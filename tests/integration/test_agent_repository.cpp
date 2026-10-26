/**
 * @file test_agent_repository.cpp
 * @brief The agent's rows against a real database: each state change happens
 *        once and only from its own state, the proxy token finds only a live
 *        run, the month's usage adds up, and a deleted row leaves its
 *        snapshots to the files sweep (migration 058). And the chat's turns
 *        (migration 059): one open turn per user, its states, the pages. And
 *        the changes the agent proposes (migration 061): answered once, by
 *        their owner, while fresh; run once; told to the next turn.
 */

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/agent/ActionRepository.hpp"
#include "repositories/agent/AgentRepository.hpp"
#include "repositories/agent/TurnRepository.hpp"
#include "test_helpers.hpp"

namespace {

using Repositories::Agent::ActionRepository;
using Repositories::Agent::AgentRepository;
using Repositories::Agent::TurnRepository;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaac5";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbd5";

class AgentRepositoryTest : public TestHelpers::CoreBackedTest {
protected:
    AgentRepository agents;

    std::string config_file_name() const override { return "agent_repository_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("DELETE FROM ani_agents");
            txn.exec("DELETE FROM ani_usage");
            txn.exec("DELETE FROM ani_actions");
            txn.exec("DELETE FROM ani_turns");
            txn.exec("DELETE FROM private_file_purges WHERE object_key LIKE 'test/%'");
            return true;
        });
    }

    std::string state(const char* owner) { return agents.get(owner)->value("state", ""); }
};

}  // namespace

TEST_F(AgentRepositoryTest, ARunGoesThroughItsStatesOnce) {
    EXPECT_FALSE(agents.get(kAnna).has_value());
    EXPECT_TRUE(agents.begin_start(kAnna));
    EXPECT_FALSE(agents.begin_start(kAnna)) << "already starting";
    EXPECT_EQ(state(kAnna), "starting");
    EXPECT_EQ(agents.count_active(), 1);

    EXPECT_FALSE(agents.set_running(kAnna, "10.42.3.7")) << "no run yet: set_run comes first";
    EXPECT_TRUE(agents.set_run(kAnna, "ani-x", "gw", "hash-1", "test/private/a/ani/"));
    EXPECT_TRUE(agents.set_running(kAnna, "10.42.3.7"));
    EXPECT_FALSE(agents.set_running(kAnna, "10.42.3.8")) << "running already";
    EXPECT_EQ(agents.get(kAnna)->value("pod_ip", ""), "10.42.3.7");

    EXPECT_FALSE(agents.set_stopped(kAnna, 1)) << "a running agent stops through stopping";
    EXPECT_TRUE(agents.begin_stop(kAnna, "the pod ended"));
    EXPECT_FALSE(agents.begin_stop(kAnna)) << "stopping already";
    EXPECT_TRUE(agents.set_stopped(kAnna, 1));
    const auto row = *agents.get(kAnna);
    EXPECT_EQ(row.value("state", ""), "stopped");
    EXPECT_EQ(row.value("generation", 0), 1);
    EXPECT_EQ(row.value("last_error", ""), "the pod ended");
    EXPECT_TRUE(row["pod_name"].is_null());
    EXPECT_EQ(agents.count_active(), 0);

    EXPECT_TRUE(agents.begin_start(kAnna)) << "a stopped agent starts again";
    EXPECT_TRUE(agents.get(kAnna)->at("last_error").is_null()) << "a new start forgets the old failure";
}

TEST_F(AgentRepositoryTest, TheProxyTokenFindsOnlyALiveRun) {
    ASSERT_TRUE(agents.begin_start(kAnna));
    ASSERT_TRUE(agents.set_run(kAnna, "ani-x", "gw", "hash-a", "test/private/a/ani/"));
    EXPECT_EQ(agents.owner_by_proxy_token("hash-a").value_or(""), kAnna);
    EXPECT_FALSE(agents.owner_by_proxy_token("hash-b").has_value());
    ASSERT_TRUE(agents.begin_stop(kAnna));
    EXPECT_FALSE(agents.owner_by_proxy_token("hash-a").has_value()) << "a stopping run calls no model";
    ASSERT_TRUE(agents.set_stopped(kAnna, 0));
    EXPECT_FALSE(agents.owner_by_proxy_token("hash-a").has_value());
}

TEST_F(AgentRepositoryTest, UsageAddsUpPerUserAndMonth) {
    EXPECT_EQ(agents.month_usage(kAnna).value("requests", -1), 0);
    agents.add_usage(kAnna, 100, 20);
    agents.add_usage(kAnna, 50, 5);
    agents.add_usage(kBoris, 7, 1);
    const auto u = agents.month_usage(kAnna);
    EXPECT_EQ(u.value("requests", 0), 2);
    EXPECT_EQ(u.value("prompt_tokens", 0), 150);
    EXPECT_EQ(u.value("completion_tokens", 0), 25);
    EXPECT_EQ(u.value("month", "").size(), 7U) << "YYYY-MM";
}

TEST_F(AgentRepositoryTest, TouchMovesOnlyALiveAgent) {
    ASSERT_TRUE(agents.begin_start(kAnna));
    Database::get().execute_write([](auto& txn) {
        txn.exec("UPDATE ani_agents SET last_active_at = now() - interval '1 hour'");
        return true;
    });
    agents.touch(kAnna);
    EXPECT_LT(agents.get(kAnna)->value("idle_seconds", 9999), 60);
    agents.touch(kBoris);
    EXPECT_FALSE(agents.get(kBoris).has_value()) << "touch creates nothing";
}

TEST_F(AgentRepositoryTest, ADeletedRowQueuesItsSnapshots) {
    ASSERT_TRUE(agents.begin_start(kAnna));
    ASSERT_TRUE(agents.set_run(kAnna, "ani-x", "gw", "hash-a", "test/private/a/ani/"));
    ASSERT_TRUE(agents.begin_stop(kAnna));
    ASSERT_TRUE(agents.set_stopped(kAnna, 3));
    const auto keys = Database::get().execute_write([](auto& txn) {
        txn.exec_params("DELETE FROM ani_agents WHERE owner_id = $1::uuid", std::string(kAnna));
        std::string out;
        for (const auto& r : txn.exec("SELECT object_key FROM private_file_purges WHERE object_key LIKE 'test/%' "
                                      "ORDER BY object_key")) {
            out += r[0].template as<std::string>() + ";";
        }
        return out;
    });
    EXPECT_EQ(keys, "test/private/a/ani/state-2.tgz;test/private/a/ani/state-3.tgz;test/private/a/ani/state-4.tgz;");
}

TEST_F(AgentRepositoryTest, ATurnRunsOnceAndOneAtATime) {
    TurnRepository turns;
    const auto t = turns.open(kAnna, "Привет", "Asia/Almaty");
    ASSERT_TRUE(t.has_value());
    const std::string id = (*t)["id"];
    EXPECT_EQ((*t)["status"], "queued");
    EXPECT_EQ((*t)["tools"], nlohmann::json::array());
    EXPECT_FALSE(turns.open(kAnna, "Ещё", "UTC").has_value()) << "one open turn per user";
    EXPECT_TRUE(turns.open(kBoris, "Hi", "UTC").has_value()) << "another user is not held up";

    const auto w = turns.for_worker(id);
    EXPECT_EQ((*w)["owner_id"], kAnna);
    EXPECT_EQ((*w)["tz"], "Asia/Almaty");
    EXPECT_FALSE(turns.get(kBoris, id).has_value()) << "not Boris's turn";

    EXPECT_FALSE(turns.finish(id, "Привет!", nlohmann::json::array())) << "not running yet";
    EXPECT_FALSE(turns.requeue(id)) << "not running yet";
    EXPECT_TRUE(turns.start(id));
    EXPECT_FALSE(turns.start(id)) << "running already";
    EXPECT_TRUE(turns.requeue(id)) << "the agent was not ready: back in the queue";
    EXPECT_EQ((*turns.get(kAnna, id))["status"], "queued");
    EXPECT_TRUE(turns.start(id));
    EXPECT_TRUE(turns.finish(id, "Привет!", nlohmann::json::array({"today"})));
    EXPECT_FALSE(turns.fail(id, "failed")) << "a done turn stays done";
    const auto done = turns.get(kAnna, id);
    EXPECT_EQ((*done)["status"], "done");
    EXPECT_EQ((*done)["reply"], "Привет!");
    EXPECT_EQ((*done)["tools"], nlohmann::json::array({"today"}));

    const auto next = turns.open(kAnna, "Ещё", "UTC");
    ASSERT_TRUE(next.has_value()) << "the chat is free again";
    const std::string next_id = (*next)["id"];
    EXPECT_TRUE(turns.fail(next_id, "busy"));
    EXPECT_EQ((*turns.get(kAnna, next_id))["error"], "busy");
    EXPECT_EQ(turns.count_since(kAnna, 3600), 2);
}

TEST_F(AgentRepositoryTest, AStaleOpenTurnDoesNotLockTheChat) {
    TurnRepository turns;
    const std::string old = (*turns.open(kAnna, "Привет", "UTC"))["id"];
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("UPDATE ani_turns SET created_at = now() - interval '1 hour' WHERE id::text = $1", old);
        return true;
    });
    ASSERT_TRUE(turns.open(kAnna, "Ты тут?", "UTC").has_value());
    EXPECT_EQ((*turns.get(kAnna, old))["status"], "failed");
    EXPECT_EQ((*turns.get(kAnna, old))["error"], "timeout");
}

TEST_F(AgentRepositoryTest, TurnsPageBackNewestFirst) {
    TurnRepository turns;
    std::vector<std::string> ids;
    for (int i = 0; i < 3; ++i) {
        const std::string id = (*turns.open(kAnna, "m" + std::to_string(i), "UTC"))["id"];
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE ani_turns SET status = 'done', created_at = now() - make_interval(mins => $2) WHERE id::text = "
                "$1",
                id,
                10 - i);
            return true;
        });
        ids.push_back(id);
    }
    const auto first = turns.list(kAnna, "", 2);
    ASSERT_EQ(first.size(), 2U);
    EXPECT_EQ(first[0]["id"], ids[2]);
    EXPECT_EQ(first[1]["id"], ids[1]);
    const auto rest = turns.list(kAnna, ids[1], 2);
    ASSERT_EQ(rest.size(), 1U);
    EXPECT_EQ(rest[0]["id"], ids[0]);
    EXPECT_TRUE(turns.list(kBoris, "", 10).empty());
}

TEST_F(AgentRepositoryTest, LocalTimeIsReadInTheZone) {
    TurnRepository turns;
    const auto t = turns.local_now("Asia/Almaty");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->size(), std::string("2026-10-25 19:41, Sun").size());
    EXPECT_FALSE(turns.local_now("Mars/Olympus").has_value());
}

TEST_F(AgentRepositoryTest, TheGatewayOfARunningAgentOnly) {
    EXPECT_FALSE(agents.gateway(kAnna).has_value());
    agents.begin_start(kAnna);
    agents.set_run(kAnna, "ani-x", "gw", "hash-g", "test/private/a/ani/");
    EXPECT_FALSE(agents.gateway(kAnna).has_value()) << "starting";
    agents.set_running(kAnna, "10.42.3.7");
    EXPECT_EQ(*agents.gateway(kAnna), (std::pair<std::string, std::string>{"10.42.3.7", "gw"}));
}

TEST_F(AgentRepositoryTest, TurnsAreDeletedOneByOneOrClearedButNotWhileOpen) {
    TurnRepository turns;
    const std::string done = (*turns.open(kAnna, "Привет", "UTC"))["id"];
    turns.start(done);
    turns.finish(done, "Привет!", nlohmann::json::array());
    const std::string open = (*turns.open(kAnna, "Ещё", "UTC"))["id"];

    EXPECT_EQ(turns.remove(kBoris, done), TurnRepository::Removed::Missing) << "not Boris's turn";
    EXPECT_EQ(turns.remove(kAnna, open), TurnRepository::Removed::Open);
    EXPECT_TRUE(turns.has_open(kAnna));
    EXPECT_EQ(turns.remove(kAnna, done), TurnRepository::Removed::Done);
    EXPECT_EQ(turns.remove(kAnna, done), TurnRepository::Removed::Missing);

    turns.fail(open, "busy");
    EXPECT_FALSE(turns.has_open(kAnna));
    (*turns.open(kBoris, "Hi", "UTC"));
    EXPECT_EQ(turns.clear(kAnna), 1);
    EXPECT_TRUE(turns.list(kAnna, "", 10).empty());
    EXPECT_EQ(turns.list(kBoris, "", 10).size(), 1U) << "Boris's chat stays";
}

TEST_F(AgentRepositoryTest, ClearingTheChatCountsAsANewConversation) {
    EXPECT_EQ(agents.chat_session(kAnna), 0);
    agents.next_chat_session(kAnna);
    EXPECT_EQ(agents.chat_session(kAnna), 1) << "a user who never woke the agent gets a row";
    EXPECT_EQ(agents.get(kAnna)->value("state", ""), "stopped");
    agents.begin_start(kAnna);
    agents.next_chat_session(kAnna);
    EXPECT_EQ(agents.chat_session(kAnna), 2);
    EXPECT_EQ(agents.get(kAnna)->value("state", ""), "starting") << "the run is untouched";
}

TEST_F(AgentRepositoryTest, AProposalIsAnsweredOnceByItsOwnerWhileFresh) {
    TurnRepository turns;
    ActionRepository actions;
    const std::string turn = (*turns.open(kAnna, "Добавь молоко", "UTC"))["id"];
    const nlohmann::json req = {{"method", "POST"}, {"path", "/api/v1/tasks/notes"}, {"body", {{"text", "молоко"}}}};
    const std::string a = actions.add(kAnna, turn, "add_note", "Note to the inbox: «молоко»", req);
    const std::string b = actions.add(kAnna, turn, "add_note", "Note to the inbox: «хлеб»", req);
    EXPECT_EQ(actions.count_for_turn(turn), 2);

    const auto view = turns.get(kAnna, turn);
    ASSERT_EQ((*view)["actions"].size(), 2U);
    EXPECT_EQ((*view)["actions"][0]["id"], a);
    EXPECT_EQ((*view)["actions"][0]["status"], "pending");
    EXPECT_FALSE((*view)["actions"][0].contains("request")) << "the panel does not see the request";

    using Answer = ActionRepository::Answer;
    EXPECT_EQ(actions.answer(kBoris, a, true), Answer::Missing) << "not Boris's";
    EXPECT_EQ(actions.answer(kAnna, a, true), Answer::Done);
    EXPECT_EQ(actions.answer(kAnna, a, false), Answer::NotPending) << "confirmed already";
    EXPECT_EQ(actions.answer(kAnna, b, false), Answer::Done);
    EXPECT_EQ(actions.get(kAnna, b)->value("status", ""), "cancelled");

    const std::string c = actions.add(kAnna, turn, "add_note", "Note to the inbox: «сыр»", req);
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("UPDATE ani_actions SET created_at = now() - interval '2 hours' WHERE id::text = $1", c);
        return true;
    });
    EXPECT_EQ(actions.get(kAnna, c)->value("status", ""), "expired");
    EXPECT_EQ(actions.answer(kAnna, c, true), Answer::Expired);
    EXPECT_EQ(actions.answer(kAnna, c, false), Answer::Done) << "an old one can still be cancelled";
}

TEST_F(AgentRepositoryTest, AConfirmedActionRunsOnceAndTheNextTurnHearsOfIt) {
    TurnRepository turns;
    ActionRepository actions;
    const std::string turn = (*turns.open(kAnna, "Отметь зарядку", "UTC"))["id"];
    const std::string a =
        actions.add(kAnna, turn, "check_habit", "Tick «Зарядка» for today", nlohmann::json{{"method", "PUT"}});
    EXPECT_FALSE(actions.start(a)) << "not confirmed";
    actions.answer(kAnna, a, true);
    const auto w = actions.for_worker(a);
    EXPECT_EQ(w->value("owner_id", ""), kAnna);
    EXPECT_EQ((*w)["request"]["method"], "PUT");
    EXPECT_TRUE(actions.start(a));
    EXPECT_FALSE(actions.start(a)) << "running already";
    EXPECT_TRUE(actions.finish(a, ""));
    EXPECT_FALSE(actions.finish(a, "failed")) << "a done action stays done";
    EXPECT_EQ(actions.get(kAnna, a)->value("status", ""), "done");

    turns.start(turn);
    turns.finish(turn, "Отметить?", nlohmann::json::array({"check_habit"}));
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("UPDATE ani_turns SET created_at = now() - interval '1 minute' WHERE id::text = $1", turn);
        return true;
    });
    const std::string next = (*turns.open(kAnna, "Готово?", "UTC"))["id"];
    EXPECT_EQ(actions.of_previous_turn(kAnna, next),
              nlohmann::json::parse(R"([{"summary":"Tick «Зарядка» for today","status":"done"}])"));
    EXPECT_EQ(actions.of_previous_turn(kAnna, turn), nlohmann::json::array()) << "no turn before the first";

    EXPECT_EQ(turns.remove(kAnna, turn), TurnRepository::Removed::Done);
    EXPECT_FALSE(actions.for_worker(a).has_value()) << "the turn takes its actions with it";
}
