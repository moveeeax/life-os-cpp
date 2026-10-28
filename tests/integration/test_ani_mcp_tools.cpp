/**
 * @file test_ani_mcp_tools.cpp
 * @brief Ani's MCP tools (src/mcp/AniTools) against a real database: the
 *        commands go into her inbox with the webhook's rules, refusals come
 *        back as tool errors, reading shows presence, items and replies,
 *        chat queues an MCP turn, and a wait for a reply is capped.
 */

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/Hook.hpp"
#include "agent/Intake.hpp"
#include "agent/Presence.hpp"
#include "cache/Cache.hpp"
#include "database/Database.hpp"
#include "mcp/AniTools.hpp"
#include "repositories/agent/AgentRepository.hpp"
#include "repositories/agent/InboxRepository.hpp"
#include "test_helpers.hpp"

namespace {

using nlohmann::json;
using Repositories::Agent::InboxRepository;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaca";

class AniMcpTools : public TestHelpers::CoreBackedTest {
protected:
    int waits = 0;

    std::string config_file_name() const override { return "ani_mcp_tools_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["jobs"]["enabled"] = true;
        cfg["ani"]["agent"]["enabled"] = true;
        cfg["ani"]["agent"]["upstream_key"] = "sk-test";
        cfg["ani"]["agent"]["proxy_url"] = "http://worker.test/v1";
        cfg["ani"]["agent"]["max_running"] = 1;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        // The real cache of the fixture: waking her agent takes its locks.
        Cache::get().del(std::string("ani:seen:") + kAnna);
        Cache::get().del(std::string("ani:hook:rate:") + kAnna);
        Database::get().execute_write([](auto& txn) {
            txn.exec("DELETE FROM ani_inbox");
            txn.exec("DELETE FROM ani_prefs");
            txn.exec("DELETE FROM ani_actions");
            txn.exec("DELETE FROM ani_turns");
            txn.exec("DELETE FROM ani_agents");
            return true;
        });
    }

    /// Calls a tool; the scheduler runs each wait at once and counts it.
    json call(const std::string& name, const json& args = json::object()) {
        Mcp::Scheduler now = [this](double, std::function<void()> fn) {
            ++waits;
            fn();
        };
        const auto server = Mcp::ani_server(kAnna, now);
        std::optional<json> out;
        server.run(name, args, [&](json r) { out = std::move(r); });
        EXPECT_TRUE(out.has_value()) << name;
        return out.value_or(json::object());
    }

    static std::string text(const json& r) { return r["content"][0]["text"].get<std::string>(); }
};

}  // namespace

TEST_F(AniMcpTools, SayGoesToInbox) {
    const json r = call("ani_say", {{"text", "Привет"}});
    EXPECT_EQ(r["isError"], false) << text(r);
    const json items = InboxRepository().items(kAnna, 5);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0]["kind"], "say");
    EXPECT_EQ(items[0]["status"], "ready");
    EXPECT_EQ(items[0]["payload"]["via"], "mcp");
}

TEST_F(AniMcpTools, ToolErrorsAreResults) {
    EXPECT_EQ(call("ani_say", {{"text", std::string(300, 'a')}})["isError"], true) << "over 200 characters";
    EXPECT_EQ(call("ani_outfit", {{"outfit", "bare"}})["isError"], true) << "bare is off";
    EXPECT_EQ(call("ani_act", {{"name", "moonwalk"}})["isError"], true) << "not one of her moves";
    for (int i = 0; i < Agent::Hook::kCallsPerHour; ++i)
        Agent::Intake::within_rate(kAnna);
    const json r = call("ani_act", {{"name", "wave"}});
    EXPECT_EQ(r["isError"], true);
    EXPECT_NE(text(r).find("120"), std::string::npos) << text(r);
}

TEST_F(AniMcpTools, PresenceOpenAndAway) {
    EXPECT_NE(text(call("ani_presence")).find("unknown"), std::string::npos);
    Agent::Presence::mark(kAnna);
    EXPECT_NE(text(call("ani_presence")).find("\"open\""), std::string::npos);
}

TEST_F(AniMcpTools, ItemsShowReplies) {
    InboxRepository inbox;
    const std::string spoken = inbox.add(kAnna, "event", json{{"text", "a"}}, std::nullopt, 86400, "thinking").id;
    const std::string quiet = inbox.add(kAnna, "event", json{{"text", "b"}}, std::nullopt, 86400, "thinking").id;
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("UPDATE ani_inbox SET status = 'ready', say = 'Ответ' WHERE id::text = $1", spoken);
        txn.exec_params("UPDATE ani_inbox SET status = 'delivered' WHERE id::text = $1", quiet);
        return true;
    });
    const std::string listed = text(call("ani_items"));
    EXPECT_NE(listed.find("Ответ"), std::string::npos);
    const std::string one = text(call("ani_item", {{"id", quiet}}));
    EXPECT_NE(one.find("delivered"), std::string::npos);
    EXPECT_EQ(call("ani_item", {{"id", "00000000-0000-4000-8000-000000000000"}})["isError"], true);
}

TEST_F(AniMcpTools, ChatQueuesMcpTurn) {
    Repositories::Agent::AgentRepository agents;
    ASSERT_TRUE(agents.begin_start(kAnna));
    ASSERT_TRUE(agents.set_run(kAnna, "ani-x", "gw-token", "hash-1", "test/private/a/ani/"));
    ASSERT_TRUE(agents.set_running(kAnna, "10.42.3.7"));
    const json r = call("ani_chat", {{"text", "Как дела?"}});
    ASSERT_EQ(r["isError"], false) << text(r);
    EXPECT_NE(text(r).find("turn_id"), std::string::npos);
    const json turns = Database::get().execute_read(
        [](auto& txn) { return json(txn.exec("SELECT origin FROM ani_turns")[0][0].template as<std::string>()); });
    EXPECT_EQ(turns, "mcp");
}

TEST_F(AniMcpTools, ReplyWaitClamped) {
    Repositories::Agent::AgentRepository agents;
    ASSERT_TRUE(agents.begin_start(kAnna));
    ASSERT_TRUE(agents.set_run(kAnna, "ani-x", "gw-token", "hash-1", "test/private/a/ani/"));
    ASSERT_TRUE(agents.set_running(kAnna, "10.42.3.7"));
    const json chat = call("ani_chat", {{"text", "Привет"}});
    const std::string body = text(chat);
    const auto at = body.find('{');
    ASSERT_NE(at, std::string::npos);
    const std::string turn_id = json::parse(body.substr(at))["turn_id"];
    waits = 0;
    const json r = call("ani_reply", {{"turn_id", turn_id}, {"wait_seconds", 90}});
    EXPECT_EQ(waits, Mcp::kReplyWaitMax) << "one check a second, 25 at most";
    EXPECT_NE(text(r).find("queued"), std::string::npos) << text(r);
}

TEST_F(AniMcpTools, ReplyLeavesNothingBehind) {
    // The reply's closures hold the caller's callback; once it answered,
    // nothing may keep it (a self-referencing closure would leak it).
    const auto server = Mcp::ani_server(kAnna, [](double, std::function<void()> fn) { fn(); });
    auto token = std::make_shared<int>(0);
    bool answered = false;
    server.run("ani_reply",
               {{"turn_id", "00000000-0000-4000-8000-000000000000"}, {"wait_seconds", 3}},
               [token, &answered](json) { answered = true; });
    EXPECT_TRUE(answered);
    EXPECT_EQ(token.use_count(), 1) << "the reply callback was not released";
}

TEST_F(AniMcpTools, NewCommandsQueue) {
    const std::vector<std::pair<std::string, json>> calls = {
        {"ani_mood", {{"level", 1}}},
        {"ani_remind", {{"text", "Встань"}, {"in_seconds", 600}}},
        {"ani_ask", {{"text", "Чай?"}}},
        {"ani_focus", {{"action", "start"}, {"minutes", 25}}},
        {"ani_card", {{"title", "CI"}, {"text", "зеленый"}}},
        {"ani_wallpaper", json::object()},
        {"ani_sound", {{"which", "off"}}},
        {"ani_hold", {{"minutes", 0}}},
    };
    for (const auto& [name, args] : calls) {
        const json r = call(name, args);
        EXPECT_EQ(r["isError"], false) << name << ": " << text(r);
    }
    const json items = InboxRepository().items(kAnna, 20);
    std::set<std::string> kinds;
    for (const auto& i : items)
        kinds.insert(i["kind"].get<std::string>());
    for (const char* k : {"mood", "remind", "ask", "focus", "card", "wallpaper", "sound", "hold"})
        EXPECT_TRUE(kinds.count(k)) << k;
}

TEST_F(AniMcpTools, AskAnswerVisible) {
    const json r = call("ani_ask", {{"text", "Чай?"}, {"options", {"Да", "Нет"}}});
    const std::string body = text(r);
    const std::string id = json::parse(body.substr(body.find('{')))["id"];
    ASSERT_EQ(Agent::Intake::answer(kAnna, id, "Да"), InboxRepository::Answer::Ok);
    EXPECT_NE(text(call("ani_item", {{"id", id}})).find("\"answer\": \"Да\""), std::string::npos);
}

TEST_F(AniMcpTools, SoundListShowsIds) {
    const json r = call("ani_sound", {{"list", true}});
    EXPECT_EQ(r["isError"], false) << text(r);
    EXPECT_NE(text(r).find('['), std::string::npos) << "a list, even an empty one";
    EXPECT_TRUE(InboxRepository().items(kAnna, 5).empty()) << "listing changes nothing";
}

TEST_F(AniMcpTools, PresenceShowsHold) {
    call("ani_hold", {{"minutes", 30}});
    EXPECT_NE(text(call("ani_presence")).find("held_until"), std::string::npos);
}

TEST_F(AniMcpTools, ProposalArgsCapped) {
    const json r = call("ani_note", {{"text", std::string(2500, 'n')}});
    EXPECT_EQ(r["isError"], true);
    EXPECT_NE(text(r).find("2000"), std::string::npos) << text(r);
}

TEST_F(AniMcpTools, WallpaperListTakesDevice) {
    const json r = call("ani_wallpaper", {{"list", true}, {"device", "phone"}});
    EXPECT_EQ(r["isError"], false) << text(r);
}
