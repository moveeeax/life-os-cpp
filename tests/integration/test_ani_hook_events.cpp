/**
 * @file test_ani_hook_events.cpp
 * @brief Webhook events through the user's agent (job ani_hook_event, then
 *        the ani_turn job of a webhook turn), against a real database and a
 *        scripted gateway: events that piled up go as one turn, oldest
 *        first; her reply becomes one line for the page, SILENT none; the
 *        user's own message is taken while a webhook turn runs and waits for
 *        it; with no agent slot the events wait until their time is out.
 */

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/Hook.hpp"
#include "agent/Turn.hpp"
#include "database/Database.hpp"
#include "jobs/Job.hpp"
#include "net/Http.hpp"
#include "repositories/agent/AgentRepository.hpp"
#include "repositories/agent/InboxRepository.hpp"
#include "repositories/agent/TurnRepository.hpp"
#include "test_helpers.hpp"

namespace {

using nlohmann::json;
using Repositories::Agent::AgentRepository;
using Repositories::Agent::InboxRepository;
using Repositories::Agent::TurnRepository;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaac7";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbd7";

/// The pod's gateway: answers every chat call with `reply` and keeps the texts it was sent.
struct ScriptedGateway : Net::Http::Transport {
    std::string reply = "Ок.";
    std::vector<std::string> texts;

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
        const json body = json::parse(r.body);
        texts.push_back(body["messages"].back().value("content", ""));
        return {200, json{{"choices", {{{"message", {{"role", "assistant"}, {"content", reply}}}}}}}.dump()};
    }
};

class AniHookEvents : public TestHelpers::CoreBackedTest {
protected:
    ScriptedGateway gateway;
    InboxRepository inbox;
    TurnRepository turns;
    AgentRepository agents;

    std::string config_file_name() const override { return "ani_hook_events_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["jobs"]["enabled"] = true;  // events queue the worker's jobs
        cfg["ani"]["agent"]["enabled"] = true;
        cfg["ani"]["agent"]["upstream_key"] = "sk-test";
        cfg["ani"]["agent"]["proxy_url"] = "http://worker.test/v1";
        cfg["ani"]["agent"]["max_running"] = 1;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("DELETE FROM ani_inbox");
            txn.exec("DELETE FROM ani_actions");
            txn.exec("DELETE FROM ani_turns");
            txn.exec("DELETE FROM ani_agents");
            txn.exec("DELETE FROM ani_usage");
            return true;
        });
        Net::Http::install_for_testing(&gateway);
    }

    void TearDown() override {
        Net::Http::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    /// The user's agent, up and reachable.
    void running(const char* owner) {
        ASSERT_TRUE(agents.begin_start(owner));
        ASSERT_TRUE(agents.set_run(owner, "ani-x", "gw-token", "hash-1", "test/private/a/ani/"));
        ASSERT_TRUE(agents.set_running(owner, "10.42.3.7"));
    }

    std::string event(const std::string& text, int age_seconds = 0, int ttl = 86400) {
        const std::string id =
            inbox.add(kAnna, "event", json{{"source", "telegram"}, {"text", text}}, std::nullopt, ttl, "thinking").id;
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE ani_inbox SET created_at = created_at - make_interval(secs => $2), expires_at = expires_at - "
                "make_interval(secs => $2) WHERE id::text = $1",
                id,
                age_seconds);
            return true;
        });
        return id;
    }

    static json run_event() { return Agent::process_hook_event(json{{"owner", kAnna}}); }

    static std::string status_of(const std::string& id) {
        return Database::get().execute_read([&](auto& txn) {
            return txn.exec_params("SELECT status FROM ani_inbox WHERE id::text = $1", id)[0][0]
                .template as<std::string>();
        });
    }

    std::string hook_turn() {
        const auto t = turns.open_hook_turn(kAnna);
        return t ? (*t)["id"].get<std::string>() : std::string();
    }
};

}  // namespace

TEST_F(AniHookEvents, EventsBatchIntoOneTurn) {
    running(kAnna);
    event("первое", 30);
    event("второе", 20);
    event("третье", 10);
    run_event();
    const std::string id = hook_turn();
    ASSERT_FALSE(id.empty());
    const std::string text = (*turns.get(kAnna, id))["text"];
    const auto a = text.find("первое");
    const auto b = text.find("второе");
    const auto c = text.find("третье");
    ASSERT_NE(c, std::string::npos) << text;
    EXPECT_LT(a, b);
    EXPECT_LT(b, c);
    EXPECT_NE(text.find("telegram"), std::string::npos) << "the source is in the line";
    EXPECT_TRUE(inbox.thinking_unlinked(kAnna).empty()) << "all three went to the turn";

    // A fourth event while the turn waits joins it.
    event("четвёртое");
    run_event();
    EXPECT_EQ(hook_turn(), id);
    EXPECT_NE((*turns.get(kAnna, id))["text"].get<std::string>().find("четвёртое"), std::string::npos);
}

TEST_F(AniHookEvents, ReplyBecomesReadyLine) {
    running(kAnna);
    const std::string a = event("Маша: созвон через 10 минут?", 5);
    const std::string b = event("Маша: ау");
    run_event();
    const std::string id = hook_turn();
    gateway.reply = "Маша зовет на созвон через 10 минут.";
    Agent::process_turn(json{{"turn", id}});
    ASSERT_EQ(gateway.texts.size(), 1u);
    EXPECT_NE(gateway.texts[0].find("SILENT"), std::string::npos) << "the agent is told how to stay silent";
    EXPECT_NE(gateway.texts[0].find("not from the person"), std::string::npos);
    EXPECT_EQ((*turns.get(kAnna, id))["status"], "done");
    EXPECT_EQ(status_of(a), "ready");
    EXPECT_EQ(status_of(b), "delivered");
    const json items = inbox.claim(kAnna, 5);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0]["say"], "Маша зовет на созвон через 10 минут.");
}

TEST_F(AniHookEvents, SilentReplyDeliversNothing) {
    running(kAnna);
    const std::string a = event("Реклама: скидки");
    run_event();
    gateway.reply = " SILENT\n";
    Agent::process_turn(json{{"turn", hook_turn().empty() ? std::string("x") : hook_turn()}});
    EXPECT_EQ(status_of(a), "delivered");
    EXPECT_TRUE(inbox.claim(kAnna, 5).empty());
}

TEST_F(AniHookEvents, UserMessageWhileHookTurnRuns) {
    running(kAnna);
    event("x");
    run_event();
    const std::string hook = hook_turn();
    ASSERT_TRUE(turns.start(hook)) << "the hook turn runs";
    const auto sent = Agent::send(kAnna, "Привет", "Asia/Almaty");
    ASSERT_EQ(sent.status, Agent::Send::Queued) << "the user is not refused";
    const std::string user = sent.turn["id"];
    EXPECT_THROW(Agent::process_turn(json{{"turn", user}}), Jobs::DeferJob) << "it waits for the hook turn";
    EXPECT_EQ((*turns.get(kAnna, user))["status"], "queued");
    turns.finish(hook, "ok", json::array());
    Agent::process_turn(json{{"turn", user}});
    EXPECT_EQ((*turns.get(kAnna, user))["status"], "done");
}

TEST_F(AniHookEvents, BusyRetriesUntilTtlThenFails) {
    running(kBoris);  // the only slot
    const std::string fresh = event("x");
    EXPECT_THROW(run_event(), Jobs::DeferJob) << "no slot: the events wait";
    EXPECT_EQ(status_of(fresh), "thinking");
    EXPECT_TRUE(hook_turn().empty());

    Database::get().execute_write([&](auto& txn) {
        txn.exec("UPDATE ani_inbox SET expires_at = now() - interval '1 second'");
        return true;
    });
    run_event();
    EXPECT_EQ(status_of(fresh), "failed") << "their time is out";
}

TEST_F(AniHookEvents, StaleHookTurnDoesNotLockTheChat) {
    running(kAnna);
    const std::string item = event("x");
    run_event();
    const std::string hook = hook_turn();
    ASSERT_TRUE(turns.start(hook));
    // Its worker died long ago: the row still says running.
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("UPDATE ani_turns SET created_at = now() - interval '20 minutes' WHERE id::text = $1", hook);
        return true;
    });
    const auto sent = Agent::send(kAnna, "Привет", "UTC");
    ASSERT_EQ(sent.status, Agent::Send::Queued);
    Agent::process_turn(json{{"turn", sent.turn["id"]}});
    EXPECT_EQ((*turns.get(kAnna, sent.turn["id"].get<std::string>()))["status"], "done") << "not held by a dead turn";
    EXPECT_EQ((*turns.get(kAnna, hook))["status"], "failed");
    EXPECT_EQ(status_of(item), "failed") << "its events fail with it";
}

TEST_F(AniHookEvents, LostQueuedHookTurnIsReplaced) {
    running(kAnna);
    event("first");
    run_event();
    const std::string lost = hook_turn();
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("UPDATE ani_turns SET created_at = now() - interval '20 minutes' WHERE id::text = $1", lost);
        return true;
    });
    const std::string next = event("second");
    run_event();
    const std::string fresh = hook_turn();
    ASSERT_FALSE(fresh.empty());
    EXPECT_NE(fresh, lost) << "the lost turn is not joined";
    EXPECT_EQ((*turns.get(kAnna, lost))["status"], "failed");
    EXPECT_EQ(status_of(next), "thinking");
    EXPECT_NE((*turns.get(kAnna, fresh))["text"].get<std::string>().find("second"), std::string::npos);
}

TEST_F(AniHookEvents, SilentWithPunctuation) {
    running(kAnna);
    const std::string a = event("Реклама");
    run_event();
    gateway.reply = "Silent.";
    Agent::process_turn(json{{"turn", hook_turn()}});
    EXPECT_EQ(status_of(a), "delivered");
    EXPECT_TRUE(inbox.claim(kAnna, 5).empty());
}
