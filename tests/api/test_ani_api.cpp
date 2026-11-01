/**
 * @file test_ani_api.cpp
 * @brief Ani's chat and agent routes as two users see them: Boris cannot read,
 *        delete, voice, confirm or cancel what is Anna's (404, and her rows
 *        stay), his wake and his clear touch only his own rows, and an API
 *        key gets 401 key_not_allowed on the page's own routes. The API runs
 *        without the upstream model key and the proxy URL, as in
 *        production, where only the ani worker holds them.
 */

#include <functional>
#include <string>
#include <utility>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/AniController.hpp"
#include "database/Database.hpp"
#include "repositories/agent/ActionRepository.hpp"
#include "repositories/agent/AgentRepository.hpp"
#include "repositories/agent/TurnRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaa0a81";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbb0a82";

using Repositories::Agent::ActionRepository;
using Repositories::Agent::AgentRepository;
using Repositories::Agent::TurnRepository;
using TestHelpers::body_of;

class AniApi : public TestHelpers::CoreBackedTest {
protected:
    Api::AniController c;

    std::string config_file_name() const override { return "ani_api_test_config.json"; }
    void config_overrides(json& cfg) override {
        cfg["jobs"]["enabled"] = true;  // wake, messages and confirms queue the worker's jobs
        cfg["ani"]["agent"]["enabled"] = true;
        // No upstream_key and no proxy_url: the API pod does not get them.
        cfg["ani"]["agent"]["upstream_key"] = "";
        cfg["ani"]["agent"]["proxy_url"] = "";
        // The voice key only lets the speech route reach its owner check; no audio is fetched.
        cfg["now"]["ani_voice"]["api_key"] = "sk-voice-test";
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            for (const char* table : {"ani_actions", "ani_turns", "ani_usage", "ani_agents"}) {
                txn.exec_params(std::string("DELETE FROM ") + table + " WHERE owner_id IN ($1::uuid, $2::uuid)",
                                std::string(kAnna),
                                std::string(kBoris));
            }
            return true;
        });
    }

    using Handler = std::function<void(const HttpRequestPtr&, Api::AniController::Callback&&)>;

    static HttpResponsePtr call(const Security::Auth::AuthPrincipal& who,
                                HttpMethod method,
                                const json& body,
                                const Handler& fn) {
        auto req = TestHelpers::authed_json(who, body, method);
        HttpResponsePtr captured;
        fn(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    static Security::Auth::AuthPrincipal key_of(const char* who) {
        auto p = TestHelpers::user(who);
        p.via_api_key = true;
        return p;
    }

    /// A pending proposal of Anna's on a turn of hers.
    static std::string annas_action(const std::string& note) {
        TurnRepository turns;
        auto turn = turns.open(kAnna, "Добавь " + note, "UTC");
        EXPECT_TRUE(turn.has_value());
        const std::string turn_id = (*turn)["id"];
        turns.start(turn_id);
        turns.finish(turn_id, "Предлагаю заметку", json::array());
        const json request = {{"method", "POST"}, {"path", "/api/v1/tasks/notes"}, {"body", {{"text", note}}}};
        return ActionRepository().add(kAnna, turn_id, "add_note", "Note to the inbox: «" + note + "»", request);
    }
};

}  // namespace

TEST_F(AniApi, TheApiWakesTheAgentWithoutTheUpstreamKey) {
    const auto woke =
        call(TestHelpers::user(kAnna), Post, json::object(), [&](auto req, auto cb) { c.wake(req, std::move(cb)); });
    ASSERT_EQ(woke->statusCode(), k202Accepted) << std::string(woke->body());
    TestHelpers::expect_2xx_matches_schema(woke, Post, "/api/v1/ani/agent/wake");
    EXPECT_EQ(body_of(woke)["data"]["state"], "starting");

    const auto boris =
        call(TestHelpers::user(kBoris), Get, json::object(), [&](auto req, auto cb) { c.agent(req, std::move(cb)); });
    ASSERT_EQ(boris->statusCode(), k200OK) << std::string(boris->body());
    EXPECT_EQ(body_of(boris)["data"]["state"], "stopped") << "Anna's wake is not Boris's agent";
    EXPECT_FALSE(AgentRepository().get(kBoris).has_value());
}

TEST_F(AniApi, AnotherUsersTurnIsNotFound) {
    const auto sent =
        call(TestHelpers::user(kAnna), Post, json{{"text", "Привет"}, {"tz", "UTC"}}, [&](auto req, auto cb) {
            c.message(req, std::move(cb));
        });
    ASSERT_EQ(sent->statusCode(), k202Accepted) << std::string(sent->body());
    TestHelpers::expect_2xx_matches_schema(sent, Post, "/api/v1/ani/messages");
    const std::string id = body_of(sent)["data"]["id"];

    const auto boris = TestHelpers::user(kBoris);
    const auto read = call(boris, Get, json::object(), [&](auto req, auto cb) { c.turn(req, std::move(cb), id); });
    EXPECT_EQ(read->statusCode(), k404NotFound) << std::string(read->body());
    EXPECT_EQ(body_of(read)["error"], "turn_not_found");
    const auto voiced = call(boris, Get, json::object(), [&](auto req, auto cb) { c.speech(req, std::move(cb), id); });
    EXPECT_EQ(voiced->statusCode(), k404NotFound) << std::string(voiced->body());
    const auto removed =
        call(boris, Delete, json::object(), [&](auto req, auto cb) { c.removeTurn(req, std::move(cb), id); });
    EXPECT_EQ(removed->statusCode(), k404NotFound) << std::string(removed->body());

    const auto list = call(boris, Get, json::object(), [&](auto req, auto cb) { c.turns(req, std::move(cb)); });
    ASSERT_EQ(list->statusCode(), k200OK) << std::string(list->body());
    EXPECT_TRUE(body_of(list)["data"].empty()) << "Boris's chat does not show Anna's turn";

    const auto cleared =
        call(boris, Delete, json::object(), [&](auto req, auto cb) { c.clearTurns(req, std::move(cb)); });
    EXPECT_EQ(cleared->statusCode(), k204NoContent) << std::string(cleared->body());
    EXPECT_TRUE(TurnRepository().get(kAnna, id).has_value()) << "clearing Boris's chat leaves Anna's turn";

    const auto own =
        call(TestHelpers::user(kAnna), Get, json::object(), [&](auto req, auto cb) { c.turn(req, std::move(cb), id); });
    EXPECT_EQ(own->statusCode(), k200OK) << std::string(own->body());
}

TEST_F(AniApi, AnotherUsersActionCannotBeConfirmedOrCancelled) {
    const std::string a = annas_action("молоко");
    const std::string b = annas_action("хлеб");
    const auto boris = TestHelpers::user(kBoris);

    const auto confirmed =
        call(boris, Post, json::object(), [&](auto req, auto cb) { c.confirmAction(req, std::move(cb), a); });
    EXPECT_EQ(confirmed->statusCode(), k404NotFound) << std::string(confirmed->body());
    EXPECT_EQ(body_of(confirmed)["error"], "action_not_found");
    const auto cancelled =
        call(boris, Post, json::object(), [&](auto req, auto cb) { c.cancelAction(req, std::move(cb), a); });
    EXPECT_EQ(cancelled->statusCode(), k404NotFound) << std::string(cancelled->body());
    EXPECT_EQ(ActionRepository().get(kAnna, a)->value("status", ""), "pending") << "Boris changed nothing";

    const auto anna = TestHelpers::user(kAnna);
    const auto ok =
        call(anna, Post, json::object(), [&](auto req, auto cb) { c.confirmAction(req, std::move(cb), a); });
    ASSERT_EQ(ok->statusCode(), k202Accepted) << std::string(ok->body());
    TestHelpers::expect_2xx_matches_schema(ok, Post, "/api/v1/ani/actions/{id}/confirm");
    const auto no = call(anna, Post, json::object(), [&](auto req, auto cb) { c.cancelAction(req, std::move(cb), b); });
    ASSERT_EQ(no->statusCode(), k200OK) << std::string(no->body());
    TestHelpers::expect_2xx_matches_schema(no, Post, "/api/v1/ani/actions/{id}/cancel");
    EXPECT_EQ(ActionRepository().get(kAnna, b)->value("status", ""), "cancelled");
}

TEST_F(AniApi, AnApiKeyIsRefusedOnThePagesRoutes) {
    const std::string a = annas_action("сыр");
    const auto key = key_of(kAnna);
    const auto sent = call(
        key, Post, json{{"text", "Привет"}, {"tz", "UTC"}}, [&](auto req, auto cb) { c.message(req, std::move(cb)); });
    EXPECT_EQ(sent->statusCode(), k401Unauthorized) << std::string(sent->body());
    EXPECT_EQ(body_of(sent)["error"], "key_not_allowed");
    const auto woke = call(key, Post, json::object(), [&](auto req, auto cb) { c.wake(req, std::move(cb)); });
    EXPECT_EQ(woke->statusCode(), k401Unauthorized) << std::string(woke->body());
    const auto confirmed =
        call(key, Post, json::object(), [&](auto req, auto cb) { c.confirmAction(req, std::move(cb), a); });
    EXPECT_EQ(confirmed->statusCode(), k401Unauthorized) << std::string(confirmed->body());

    EXPECT_EQ(ActionRepository().get(kAnna, a)->value("status", ""), "pending");
    EXPECT_FALSE(AgentRepository().get(kAnna).has_value()) << "the key woke nothing";
    EXPECT_EQ(TurnRepository().count_since(kAnna, 3600), 1) << "only the turn of the proposal";
}
