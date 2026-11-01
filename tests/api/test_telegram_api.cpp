/**
 * @file test_telegram_api.cpp
 * @brief The owner's Telegram through the API with a scripted gateway: only
 *        the linked user reads and sends, so a second user and an unlinked
 *        admin get 404 telegram_not_linked on every chat route and the
 *        gateway is never called for them; another admin cannot take or drop
 *        the link; linking and the status are admin routes; the linked owner
 *        reads the chat list and sends.
 */

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/TelegramController.hpp"
#include "database/Database.hpp"
#include "net/Http.hpp"
#include "repositories/telegram/LinkRepository.hpp"
#include "test_fixtures.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaa7a91";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbb7a92";

using Repositories::Telegram::LinkRepository;
using TestHelpers::body_of;

/// The gateway: answers /v1/me, the chat list and a send; records every call.
struct ScriptedGateway : Net::Http::Transport {
    std::vector<std::string> calls;

    Net::Http::Response get(const std::string&, const std::vector<std::pair<std::string, std::string>>&) override {
        return {404, ""};
    }
    Net::Http::Response post_json(const std::string&,
                                  const std::string&,
                                  const std::vector<std::pair<std::string, std::string>>&,
                                  long) override {
        return {404, ""};
    }
    Net::Http::Response send(const Net::Http::Request& r) override {
        calls.push_back(r.method + " " + r.url);
        const std::string base = "http://tg.test";
        if (r.method == "GET" && r.url.rfind(base + "/v1/me", 0) == 0) {
            return {200, json{{"ok", true}, {"data", {{"id", "777"}, {"username", "boris"}}}}.dump()};
        }
        if (r.method == "GET" && r.url.rfind(base + "/v1/chats?", 0) == 0) {
            return {200,
                    json{{"ok", true},
                         {"data",
                          json::array({{{"id", "10"}, {"title", "Mum"}, {"type", "private"}, {"unread_count", 2}}})}}
                        .dump()};
        }
        if (r.method == "POST" && r.url.rfind(base + "/v1/chats/10/messages", 0) == 0) {
            return {200, json{{"ok", true}, {"data", {{"id", "501"}}}}.dump()};
        }
        return {404, json{{"ok", false}, {"error", {{"code", "NOT_FOUND"}}}}.dump()};
    }
};

Security::Auth::AuthPrincipal admin(const char* who) {
    return TestFixtures::admin_principal(who);
}

class TelegramApi : public TestHelpers::CoreBackedTest {
protected:
    Api::TelegramController c;
    ScriptedGateway gateway;

    std::string config_file_name() const override { return "telegram_api_test_config.json"; }
    void config_overrides(json& cfg) override {
        cfg["ani"]["telegram"]["url"] = "http://tg.test";
        cfg["ani"]["telegram"]["token"] = "axdz5x7wbgo.test";
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("DELETE FROM telegram_cursors");
            txn.exec("DELETE FROM telegram_waiting");
            txn.exec("DELETE FROM telegram_quiet");
            txn.exec("DELETE FROM telegram_link");
            return true;
        });
        ASSERT_TRUE(LinkRepository().set(kAnna, "555", "anna")) << "Anna's account is the linked one";
        Net::Http::install_for_testing(&gateway);
    }

    void TearDown() override {
        Net::Http::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    using Handler = std::function<void(const HttpRequestPtr&, Api::TelegramController::Callback&&)>;

    static HttpResponsePtr call(const Security::Auth::AuthPrincipal& who,
                                HttpMethod method,
                                const json& body,
                                const Handler& fn,
                                const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto req = TestHelpers::authed_json(who, body, method);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        fn(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    /// Every chat route for @p who, each expected to answer 404 telegram_not_linked.
    void expect_not_linked_everywhere(const Security::Auth::AuthPrincipal& who) {
        const std::vector<std::pair<const char*, HttpResponsePtr>> answers = {
            {"chats", call(who, Get, json::object(), [&](auto req, auto cb) { c.chats(req, std::move(cb)); })},
            {"chat", call(who, Get, json::object(), [&](auto req, auto cb) { c.chat(req, std::move(cb), "10"); })},
            {"messages",
             call(who, Get, json::object(), [&](auto req, auto cb) { c.messages(req, std::move(cb), "10"); })},
            {"send",
             call(who, Post, json{{"text", "Привет"}}, [&](auto req, auto cb) { c.send(req, std::move(cb), "10"); })},
            {"read", call(who, Post, json::object(), [&](auto req, auto cb) { c.read(req, std::move(cb), "10"); })},
            {"search",
             call(who, Get, json::object(), [&](auto req, auto cb) { c.search(req, std::move(cb)); }, {{"q", "мама"}})},
            {"waiting", call(who, Get, json::object(), [&](auto req, auto cb) { c.waiting(req, std::move(cb)); })},
            {"quiet", call(who, Get, json::object(), [&](auto req, auto cb) { c.quietList(req, std::move(cb)); })},
            {"self",
             call(who, Post, json{{"text", "Заметка"}}, [&](auto req, auto cb) { c.toSelf(req, std::move(cb)); })},
        };
        for (const auto& [route, r] : answers) {
            ASSERT_TRUE(r) << route;
            EXPECT_EQ(r->statusCode(), k404NotFound) << route << ": " << std::string(r->body());
            EXPECT_EQ(body_of(r)["error"], "telegram_not_linked") << route;
        }
    }
};

}  // namespace

TEST_F(TelegramApi, AnotherUserGets404OnEveryChatRouteAndTheGatewayIsNotCalled) {
    expect_not_linked_everywhere(TestHelpers::user(kBoris));
    EXPECT_TRUE(gateway.calls.empty()) << "nothing of Anna's account was fetched for Boris";
}

TEST_F(TelegramApi, AnUnlinkedAdminIsRefusedTheSameWay) {
    expect_not_linked_everywhere(admin(kBoris));
    EXPECT_TRUE(gateway.calls.empty());
}

TEST_F(TelegramApi, AnotherAdminCannotTakeOrDropTheLink) {
    const auto status =
        call(admin(kBoris), Get, json::object(), [&](auto req, auto cb) { c.status(req, std::move(cb)); });
    ASSERT_EQ(status->statusCode(), k200OK) << std::string(status->body());
    TestHelpers::expect_2xx_matches_schema(status, Get, "/api/v1/telegram");
    EXPECT_EQ(body_of(status)["data"]["linked"], false);
    EXPECT_EQ(body_of(status)["data"]["elsewhere"], true);
    EXPECT_TRUE(body_of(status)["data"]["link"].is_null()) << "Anna's link details are not shown to Boris";

    const auto taken = call(admin(kBoris), Put, json::object(), [&](auto req, auto cb) { c.link(req, std::move(cb)); });
    EXPECT_EQ(taken->statusCode(), k409Conflict) << std::string(taken->body());
    EXPECT_EQ(body_of(taken)["error"], "telegram_linked_elsewhere");

    const auto dropped =
        call(admin(kBoris), Delete, json::object(), [&](auto req, auto cb) { c.unlink(req, std::move(cb)); });
    EXPECT_EQ(dropped->statusCode(), k404NotFound) << std::string(dropped->body());

    const auto link = LinkRepository().current();
    ASSERT_TRUE(link.has_value());
    EXPECT_EQ(link->owner, kAnna) << "the link stays Anna's";
}

TEST_F(TelegramApi, LinkingAndTheStatusAreAdminRoutes) {
    const auto status =
        call(TestHelpers::user(kBoris), Get, json::object(), [&](auto req, auto cb) { c.status(req, std::move(cb)); });
    EXPECT_EQ(status->statusCode(), k403Forbidden) << std::string(status->body());
    const auto link =
        call(TestHelpers::user(kBoris), Put, json::object(), [&](auto req, auto cb) { c.link(req, std::move(cb)); });
    EXPECT_EQ(link->statusCode(), k403Forbidden) << std::string(link->body());
    const auto unlink = call(
        TestHelpers::user(kAnna), Delete, json::object(), [&](auto req, auto cb) { c.unlink(req, std::move(cb)); });
    EXPECT_EQ(unlink->statusCode(), k403Forbidden) << "unlinking is an admin route too";
    EXPECT_TRUE(LinkRepository().of(kAnna).has_value());
    EXPECT_TRUE(gateway.calls.empty());
}

TEST_F(TelegramApi, TheLinkedOwnerReadsAndSends) {
    const auto anna = TestHelpers::user(kAnna);
    const auto chats = call(anna, Get, json::object(), [&](auto req, auto cb) { c.chats(req, std::move(cb)); });
    ASSERT_EQ(chats->statusCode(), k200OK) << std::string(chats->body());
    TestHelpers::expect_2xx_matches_schema(chats, Get, "/api/v1/telegram/chats");
    ASSERT_EQ(body_of(chats)["data"].size(), 1U);
    EXPECT_EQ(body_of(chats)["data"][0]["title"], "Mum");

    const auto sent =
        call(anna, Post, json{{"text", "Привет"}}, [&](auto req, auto cb) { c.send(req, std::move(cb), "10"); });
    ASSERT_EQ(sent->statusCode(), k200OK) << std::string(sent->body());
    TestHelpers::expect_2xx_matches_schema(sent, Post, "/api/v1/telegram/chats/{id}/messages");
    EXPECT_EQ(body_of(sent)["data"]["sent"], true);
    ASSERT_EQ(gateway.calls.size(), 2U);
    EXPECT_EQ(gateway.calls[1].rfind("POST http://tg.test/v1/chats/10/messages", 0), 0U) << gateway.calls[1];
}
