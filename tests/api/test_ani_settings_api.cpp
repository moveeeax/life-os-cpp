/**
 * @file test_ani_settings_api.cpp
 * @brief Ani's settings on the server: the bare outfit is off until the user
 *        turns it on saying they are an adult; then the model and its body
 *        texture come as short signed links to the private part of the
 *        bucket, never cached. The webhook URL is made on the first read and
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
#include "storage/ObjectStore.hpp"
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

/// Signs links and remembers which keys it signed.
class FakeStore : public Storage::ObjectStore {
public:
    mutable std::vector<std::pair<std::string, long>> signed_downloads;  // key, seconds

    std::string upload_url(const std::string& key, const std::string&, long long) const override {
        return "https://fake/public-put/" + key;
    }
    Storage::ObjectInfo head(const std::string&) const override { return {true, 1}; }
    void remove(const std::string&) const override {}
    std::string public_url(const std::string& key) const override { return "https://cdn.test/" + key; }
    std::string key_for(const std::string& folder, const std::string& id, const std::string& name) const override {
        return "life-os/" + folder + "/" + id + "/" + name;
    }
    std::string private_key(const std::string& owner, const std::string& id, const std::string& name) const override {
        return "life-os/private/" + owner + "/" + id + "/" + name;
    }
    std::string private_upload_url(const std::string& key, const std::string&, long long) const override {
        return "https://fake/put/" + key;
    }
    std::string private_put_url(const std::string& key, long) const override {
        return "https://fake/private-put/" + key;
    }
    std::string download_url(const std::string& key, long seconds, const std::string&) const override {
        signed_downloads.emplace_back(key, seconds);
        return "https://fake/get/" + key + "?X-Amz-Expires=" + std::to_string(seconds);
    }
    bool publicly_readable(const std::string&) const override { return false; }
};

class AniSettingsApi : public TestHelpers::CoreBackedTest {
protected:
    Api::AniSettingsController c;
    FakeStore store;

    std::string config_file_name() const override { return "ani_settings_api_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec_params("DELETE FROM ani_prefs WHERE owner_id = $1::uuid", std::string(kAnna));
            txn.exec_params("DELETE FROM ani_hooks WHERE owner_id = $1::uuid", std::string(kAnna));
            txn.exec_params("DELETE FROM ani_inbox WHERE owner_id = $1::uuid", std::string(kAnna));
            return true;
        });
        Storage::install_object_store_for_testing(&store);
    }

    void TearDown() override {
        Storage::install_object_store_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
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
    HttpResponsePtr get_bare() {
        return call(
            Get, json::object(), [&](auto req, auto cb) { c.bare(req, std::move(cb)); }, "/api/v1/ani/bare");
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
    EXPECT_EQ(body_of(r)["data"], json({{"bare", false}}));
}

TEST_F(AniSettingsApi, BareNeedsAdult) {
    auto r = put_prefs(json{{"bare", true}});
    EXPECT_EQ(r->statusCode(), k422UnprocessableEntity) << std::string(r->body());
    EXPECT_EQ(body_of(r)["error"]["code"], "adult_required");
    EXPECT_EQ(body_of(get_prefs())["data"]["bare"], false);

    auto bad = put_prefs(json{{"bare", "yes"}});
    EXPECT_EQ(bad->statusCode(), k400BadRequest) << std::string(bad->body());
}

TEST_F(AniSettingsApi, BareOnGivesPresignedUrls) {
    auto on = put_prefs(json{{"bare", true}, {"adult", true}});
    ASSERT_EQ(on->statusCode(), k200OK) << std::string(on->body());
    EXPECT_EQ(body_of(on)["data"]["bare"], true);

    auto r = get_bare();
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    const json d = body_of(r)["data"];
    EXPECT_EQ(d["model_url"], "https://fake/get/life-os/private/ani/bare-v1/Ani.pmx?X-Amz-Expires=120");
    EXPECT_EQ(d["texture_url"], "https://fake/get/life-os/private/ani/bare-v1/kanata_body.png?X-Amz-Expires=120");
    EXPECT_EQ(d["expires_in"], 120);
    EXPECT_EQ(r->getHeader("Cache-Control"), "no-store");

    auto off = put_prefs(json{{"bare", false}});
    ASSERT_EQ(off->statusCode(), k200OK);
    EXPECT_EQ(get_bare()->statusCode(), k403Forbidden);
}

TEST_F(AniSettingsApi, BareOffIs403) {
    auto r = get_bare();
    EXPECT_EQ(r->statusCode(), k403Forbidden) << std::string(r->body());
    EXPECT_EQ(body_of(r)["error"]["code"], "bare_off");
    EXPECT_TRUE(store.signed_downloads.empty()) << "nothing signed while off";
}

TEST_F(AniSettingsApi, ApiKeyRefused) {
    auto r = call(
        Get, json::object(), [&](auto req, auto cb) { c.prefs(req, std::move(cb)); }, nullptr, true);
    EXPECT_EQ(r->statusCode(), k401Unauthorized);
    EXPECT_EQ(body_of(r)["error"]["code"], "key_not_allowed");
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
