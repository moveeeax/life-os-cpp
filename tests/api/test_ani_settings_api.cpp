/**
 * @file test_ani_settings_api.cpp
 * @brief Ani's settings on the server: the kinds of her lines muted; the
 *        removed bare outfit is always off and its route answers 410. The webhook URL is made on the first read and
 *        changes when replaced. A browser session only (an API key gets 401).
 */

#include <map>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/AniSettingsController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaa0a61";

Security::Auth::AuthPrincipal principal(const char* who, bool via_key = false) {
    Security::Auth::AuthPrincipal p;
    p.subject = who;
    p.via_api_key = via_key;
    p.raw_claims = json{{"sub", who}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

using TestHelpers::body_of;
using Fn = std::function<void(const HttpRequestPtr&, Api::AniSettingsController::Callback&&)>;

class AniSettingsApi : public TestHelpers::CoreBackedTest {
protected:
    Api::AniSettingsController c;

    std::string config_file_name() const override { return "ani_settings_api_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec_params("DELETE FROM ani_prefs WHERE owner_id = $1::uuid", std::string(kAnna));
            txn.exec_params("DELETE FROM ani_hooks WHERE owner_id = $1::uuid", std::string(kAnna));
            txn.exec_params("DELETE FROM ani_inbox WHERE owner_id = $1::uuid", std::string(kAnna));
            txn.exec_params("DELETE FROM ani_reminders WHERE owner_id = $1::uuid", std::string(kAnna));
            return true;
        });
    }

    HttpResponsePtr call(
        HttpMethod method, const json& body, const Fn& fn, const char* spec_path = nullptr, bool via_key = false) {
        auto req = TestHelpers::authed_json(principal(kAnna, via_key), body, method);
        HttpResponsePtr captured;
        fn(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        if (captured && spec_path) {
            TestHelpers::expect_2xx_matches_schema(captured, method, spec_path);
        }
        return captured;
    }

    HttpResponsePtr get_prefs() {
        return call(
            Get, json::object(), [&](auto req, auto cb) { c.prefs(req, std::move(cb)); }, "/api/v1/ani/prefs");
    }
    HttpResponsePtr put_prefs(const json& body) {
        return call(
            Put, body, [&](auto req, auto cb) { c.savePrefs(req, std::move(cb)); }, "/api/v1/ani/prefs");
    }
    HttpResponsePtr get_hook() {
        return call(
            Get, json::object(), [&](auto req, auto cb) { c.webhook(req, std::move(cb)); }, "/api/v1/ani/webhook");
    }
};

}  // namespace

TEST_F(AniSettingsApi, PrefsDefaultOff) {
    auto r = get_prefs();
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    EXPECT_EQ(body_of(r)["data"], json({{"bare", false}, {"muted", json::array()}}));
}

TEST_F(AniSettingsApi, MutedIsSaved) {
    auto r = put_prefs(json{{"muted", {"brief", "webhook"}}});
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    EXPECT_EQ(body_of(get_prefs())["data"], json({{"bare", false}, {"muted", {"brief", "webhook"}}}));
    EXPECT_EQ(put_prefs(json{{"muted", {"poems"}}})->statusCode(), k400BadRequest);
}

TEST_F(AniSettingsApi, BareIsGone) {
    // The old switch is not a setting any more: without muted the body says nothing.
    auto r = put_prefs(json{{"bare", true}, {"adult", true}});
    EXPECT_EQ(r->statusCode(), k400BadRequest) << std::string(r->body());
    EXPECT_EQ(body_of(get_prefs())["data"]["bare"], false);
    auto gone = call(Get, json::object(), [&](auto req, auto cb) { c.bare(req, std::move(cb)); });
    EXPECT_EQ(gone->statusCode(), k410Gone) << std::string(gone->body());
    EXPECT_EQ(body_of(gone)["error"], "bare_removed");
}

TEST_F(AniSettingsApi, ApiKeyRefused) {
    auto r = call(
        Get, json::object(), [&](auto req, auto cb) { c.prefs(req, std::move(cb)); }, nullptr, true);
    EXPECT_EQ(r->statusCode(), k401Unauthorized);
    EXPECT_EQ(body_of(r)["error"], "key_not_allowed");
    auto b = call(
        Get, json::object(), [&](auto req, auto cb) { c.bare(req, std::move(cb)); }, nullptr, true);
    EXPECT_EQ(b->statusCode(), k401Unauthorized);
}

TEST_F(AniSettingsApi, HookUrlCreatedOnFirstRead) {
    auto first = get_hook();
    ASSERT_EQ(first->statusCode(), k200OK) << std::string(first->body());
    const std::string url = body_of(first)["data"]["url"];
    EXPECT_TRUE(std::regex_search(url, std::regex("/api/v1/ani/hook/aniwh_[0-9a-f]{48}$"))) << url;
    EXPECT_TRUE(body_of(first)["data"]["recent"].is_array());
    EXPECT_EQ(body_of(get_hook())["data"]["url"], url) << "the same URL every time";
}

TEST_F(AniSettingsApi, RotateChangesUrl) {
    const std::string before = body_of(get_hook())["data"]["url"];
    auto r = call(
        Post,
        json::object(),
        [&](auto req, auto cb) { c.rotateWebhook(req, std::move(cb)); },
        "/api/v1/ani/webhook/rotate");
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    const std::string after = body_of(r)["data"]["url"];
    EXPECT_NE(after, before);
    EXPECT_EQ(body_of(get_hook())["data"]["url"], after);
}

TEST_F(AniSettingsApi, RemindersRoute) {
    const auto add = [&](const json& body) {
        return call(
            Post, body, [&](auto req, auto cb) { c.addReminder(req, std::move(cb)); }, "/api/v1/ani/reminders");
    };
    auto made = add({{"text", "Выпей воды"}, {"time", "10:00"}, {"days", {1, 2, 3, 4, 5}}, {"tz", "Asia/Almaty"}});
    ASSERT_EQ(made->statusCode(), k201Created) << std::string(made->body());
    EXPECT_EQ(body_of(made)["data"]["days"], 31);
    EXPECT_EQ(body_of(made)["data"]["time"], "10:00");
    EXPECT_EQ(add({{"text", "x"}, {"time", "9:00"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(add({{"text", "x"}, {"time", "09:00"}, {"days", {8}}})->statusCode(), k400BadRequest);
    EXPECT_EQ(add({{"text", "x"}, {"time", "09:00"}, {"tz", "Mars/Base"}})->statusCode(), k400BadRequest);

    auto list = call(
        Get, json::object(), [&](auto req, auto cb) { c.reminders(req, std::move(cb)); }, "/api/v1/ani/reminders");
    ASSERT_EQ(body_of(list)["data"].size(), 1u);
    const std::string id = body_of(made)["data"]["id"];
    auto gone = call(Delete, json::object(), [&](auto req, auto cb) { c.removeReminder(req, std::move(cb), id); });
    EXPECT_EQ(gone->statusCode(), k204NoContent);
    auto again = call(Delete, json::object(), [&](auto req, auto cb) { c.removeReminder(req, std::move(cb), id); });
    EXPECT_EQ(again->statusCode(), k404NotFound);
    auto key = call(
        Get, json::object(), [&](auto req, auto cb) { c.reminders(req, std::move(cb)); }, nullptr, true);
    EXPECT_EQ(key->statusCode(), k401Unauthorized);
}
