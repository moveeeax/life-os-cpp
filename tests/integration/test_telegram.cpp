/**
 * @file test_telegram.cpp
 * @brief The owner's Telegram against a real database and a scripted
 *        gateway: the first poll only sets the cursors, a later one turns new
 *        private messages and @mentions into one Ani event and not again, an
 *        unlinked site polls nothing, and a Telegram proposal takes the
 *        person's own text before Confirm while other actions do not.
 */

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/TelegramPoll.hpp"
#include "database/Database.hpp"
#include "net/Http.hpp"
#include "repositories/agent/ActionRepository.hpp"
#include "repositories/agent/TurnRepository.hpp"
#include "repositories/telegram/LinkRepository.hpp"
#include "test_helpers.hpp"

namespace {

using nlohmann::json;
using Repositories::Telegram::LinkRepository;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaa7e1e";

json message(long long id, const std::string& text, bool outgoing = false) {
    return json{{"id", std::to_string(id)},
                {"date", 2000000000},
                {"is_outgoing", outgoing},
                {"content", {{"type", "text"}, {"text", text}}}};
}

struct ScriptedTelegram : Net::Http::Transport {
    json chats = json::array();
    std::map<std::string, json> history;
    std::vector<std::string> urls;

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
        urls.push_back(r.method + " " + r.url);
        const std::string base = "http://tg.test";
        if (r.url.rfind(base + "/v1/chats?", 0) == 0) {
            return {200, json{{"ok", true}, {"data", chats}}.dump()};
        }
        for (const auto& [id, list] : history) {
            if (r.url.rfind(base + "/v1/chats/" + id + "/messages?", 0) == 0) {
                return {200, json{{"ok", true}, {"data", list}}.dump()};
            }
        }
        return {404, json{{"ok", false}, {"error", {{"code", "NOT_FOUND"}}}}.dump()};
    }
};

class TelegramPollDb : public TestHelpers::CoreBackedTest {
protected:
    ScriptedTelegram telegram;
    LinkRepository links;

    std::string config_file_name() const override { return "telegram_test_config.json"; }
    void config_overrides(json& cfg) override {
        cfg["jobs"]["enabled"] = true;
        cfg["ani"]["agent"]["enabled"] = true;
        cfg["ani"]["agent"]["upstream_key"] = "sk-test";
        cfg["ani"]["agent"]["proxy_url"] = "http://worker.test/v1";
        cfg["ani"]["telegram"]["url"] = "http://tg.test";
        cfg["ani"]["telegram"]["token"] = "axdz5x7wbgo.test";
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("DELETE FROM telegram_cursors");
            txn.exec("DELETE FROM telegram_link");
            txn.exec("DELETE FROM ani_inbox");
            txn.exec("DELETE FROM ani_actions");
            txn.exec("DELETE FROM ani_turns");
            return true;
        });
        telegram.chats = json::array({
            {{"id", "10"}, {"title", "Anna"}, {"type", "private"}, {"unread_count", 0}},
            {{"id", "-100"}, {"title", "Work"}, {"type", "supergroup"}, {"unread_count", 0}},
            {{"id", "-200"}, {"title", "News"}, {"type", "channel"}, {"unread_count", 5}},
        });
        telegram.history["10"] = json::array({message(100, "старое")});
        telegram.history["-100"] = json::array({message(50, "@moveeeax давно")});
        telegram.history["-200"] = json::array({message(9, "@moveeeax новости")});
        Net::Http::install_for_testing(&telegram);
    }

    void TearDown() override {
        Net::Http::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    static int events() {
        return Database::get().execute_read([](auto& txn) {
            return txn
                .exec_params("SELECT count(*) FROM ani_inbox WHERE owner_id = $1::uuid AND kind = 'event'",
                             std::string(kAnna))[0][0]
                .template as<int>();
        });
    }

    static json last_event() {
        return Database::get().execute_read([](auto& txn) {
            return json::parse(txn.exec_params("SELECT payload::text FROM ani_inbox WHERE owner_id = $1::uuid AND "
                                               "kind = 'event' ORDER BY created_at DESC LIMIT 1",
                                               std::string(kAnna))[0][0]
                                   .template as<std::string>());
        });
    }
};

}  // namespace

TEST_F(TelegramPollDb, NotLinkedPollsNothing) {
    const auto out = Agent::TelegramPoll::process_poll(json::object());
    EXPECT_EQ(out["reason"], "not_linked");
    EXPECT_TRUE(telegram.urls.empty());
}

TEST_F(TelegramPollDb, FirstPollOnlySetsTheCursors) {
    ASSERT_TRUE(links.set(kAnna, "1389231240", "moveeeax"));
    const auto out = Agent::TelegramPoll::process_poll(json::object());
    EXPECT_EQ(out["status"], "done");
    EXPECT_EQ(events(), 0) << "old messages are not told";
    const auto cursors = links.cursors(kAnna);
    EXPECT_EQ(cursors.at("10"), 100);
    EXPECT_EQ(cursors.at("-100"), 50);
    EXPECT_EQ(cursors.count("-200"), 0u) << "channels are skipped";
    EXPECT_TRUE(links.current()->primed);
    EXPECT_FALSE(links.set("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbb7e1e", "1", "x")) << "one user at most";
}

TEST_F(TelegramPollDb, NewDmsAndMentionsBecomeOneEvent) {
    ASSERT_TRUE(links.set(kAnna, "1389231240", "moveeeax"));
    Agent::TelegramPoll::process_poll(json::object());
    telegram.chats[0]["unread_count"] = 2;
    telegram.chats[1]["unread_count"] = 3;
    telegram.history["10"] = json::array({message(103, "ты где?"),
                                          message(102, "моё", /*outgoing=*/true),
                                          message(101, "привет"),
                                          message(100, "старое")});
    telegram.history["-100"] =
        json::array({message(53, "обед?"), message(52, "@moveeeax глянь PR"), message(50, "@moveeeax давно")});

    const auto out = Agent::TelegramPoll::process_poll(json::object());
    EXPECT_EQ(out["new"], 3);
    ASSERT_EQ(events(), 1) << "one event for the whole poll";
    const json e = last_event();
    EXPECT_EQ(e["source"], "telegram");
    EXPECT_EQ(e["via"], "telegram");
    const std::string text = e["text"];
    EXPECT_NE(text.find("Anna: привет"), std::string::npos);
    EXPECT_NE(text.find("Anna: ты где?"), std::string::npos);
    EXPECT_NE(text.find("Work (mentions you): @moveeeax глянь PR"), std::string::npos);
    EXPECT_EQ(text.find("обед"), std::string::npos) << "a group message without the name";
    EXPECT_EQ(text.find("моё"), std::string::npos) << "the owner's own message";
    EXPECT_EQ(links.cursors(kAnna).at("10"), 103);

    Agent::TelegramPoll::process_poll(json::object());
    EXPECT_EQ(events(), 1) << "the same messages are not told twice";
}

TEST_F(TelegramPollDb, OnlyATelegramProposalTakesANewText) {
    Repositories::Agent::TurnRepository turns;
    const auto t = turns.open(kAnna, "Ответь Анне", "UTC");
    ASSERT_TRUE(t.has_value());
    const std::string turn = (*t)["id"].get<std::string>();
    Repositories::Agent::ActionRepository actions;
    const std::string send = actions.add(kAnna,
                                         turn,
                                         "tg_send",
                                         "Send to Anna: «Буду в семь»",
                                         json{{"method", "POST"},
                                              {"path", "/api/v1/telegram/chats/10/messages"},
                                              {"body", {{"text", "Буду в семь"}, {"key", "ani-1"}}},
                                              {"label", "Send to Anna: "}});
    const std::string note = actions.add(
        kAnna, turn, "add_note", "Note to the inbox: «x»", json{{"method", "POST"}, {"body", {{"text", "x"}}}});

    EXPECT_TRUE(actions.retext(kAnna, send, "Буду в восемь, «пробки»"));
    const auto w = actions.for_worker(send);
    EXPECT_EQ((*w)["request"]["body"]["text"], "Буду в восемь, «пробки»");
    EXPECT_EQ((*w)["request"]["body"]["key"], "ani-1") << "the key stays";
    EXPECT_EQ((*actions.get(kAnna, send))["summary"], "Send to Anna: «Буду в восемь, «пробки»»");
    EXPECT_EQ((*actions.get(kAnna, send))["text"], "Буду в восемь, «пробки»") << "the page edits this";
    EXPECT_TRUE((*actions.get(kAnna, note))["text"].is_null());
    EXPECT_FALSE(actions.retext(kAnna, note, "y")) << "not a Telegram message";
    EXPECT_FALSE(actions.retext("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbb7e1e", send, "y")) << "not theirs";
    ASSERT_EQ(actions.answer(kAnna, send, /*confirm=*/true), Repositories::Agent::ActionRepository::Answer::Done);
    EXPECT_FALSE(actions.retext(kAnna, send, "поздно")) << "confirmed already";
}
