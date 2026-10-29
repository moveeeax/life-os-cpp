/**
 * @file test_telegram_poll.cpp
 * @brief What the Telegram poll keeps of a chat (agent/TelegramPoll.hpp), how
 *        its event reads, and the site requests Ani's Telegram tools make
 *        (agent/TelegramTools.hpp).
 */

#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/TelegramPoll.hpp"
#include "agent/TelegramTools.hpp"
#include "telegram/Gateway.hpp"

namespace {

using nlohmann::json;
using namespace Agent::TelegramPoll;

json message(const std::string& id, const std::string& text, bool outgoing = false, long long date = 2000000000) {
    return json{{"id", id},
                {"chat_id", "1"},
                {"date", date},
                {"is_outgoing", outgoing},
                {"content", {{"type", "text"}, {"text", text}}}};
}

}  // namespace

TEST(TelegramPoll, MentionsOnAWordBoundary) {
    EXPECT_TRUE(mentions("@moveeeax глянь", "moveeeax"));
    EXPECT_TRUE(mentions("привет, @MoveeeAx!", "moveeeax"));
    EXPECT_FALSE(mentions("@moveeeax_bot", "moveeeax")) << "a longer name";
    EXPECT_FALSE(mentions("mail@moveeeax", "moveeeax")) << "inside a word";
    EXPECT_FALSE(mentions("moveeeax", "moveeeax")) << "no @";
    EXPECT_FALSE(mentions("@anything", "")) << "an account without a username";
}

TEST(TelegramPoll, KeepsNewIncomingPastTheCursor) {
    const json dm = {{"id", "10"}, {"title", "Anna"}, {"type", "private"}};
    const json list = json::array({message("105", "ещё"),
                                   message("104", "моё", /*outgoing=*/true),
                                   message("103", "привет"),
                                   message("100", "старое")});
    const auto got = addressed(dm, list, 100, "moveeeax", 0);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0].id, 103) << "oldest first";
    EXPECT_EQ(got[0].kind, "dm");
    EXPECT_EQ(got[1].text, "ещё");
    EXPECT_EQ(got[0].chat_title, "Anna");
}

TEST(TelegramPoll, GroupsOnlyForMentionsAndNoChannels) {
    const json group = {{"id", "-100"}, {"title", "Work"}, {"type", "supergroup"}};
    const json list = json::array({message("7", "@moveeeax посмотри PR"), message("6", "обед?")});
    const auto got = addressed(group, list, 5, "moveeeax", 0);
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0].kind, "mention");
    const json channel = {{"id", "-200"}, {"title", "News"}, {"type", "channel"}};
    EXPECT_TRUE(addressed(channel, list, 0, "moveeeax", 0).empty());
}

TEST(TelegramPoll, AnUnknownChatKeepsOnlyRecentOnes) {
    const json dm = {{"id", "11"}, {"title", "Boris"}, {"type", "private"}};
    const json list = json::array({message("3", "сейчас", false, 2000000500), message("2", "давно", false, 1000)});
    const auto got = addressed(dm, list, 0, "moveeeax", 2000000000);
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0].text, "сейчас");
}

TEST(TelegramPoll, EventTextNamesTenAndCounts) {
    std::vector<Incoming> all;
    for (int i = 0; i < 12; ++i) {
        all.push_back(Incoming{"1", "Anna", i + 1, 0, i == 0 ? "mention" : "dm", "сообщение " + std::to_string(i)});
    }
    all[1].text = std::string(400, 'x');
    const std::string text = event_text(all);
    EXPECT_NE(text.find("Anna [chat 1] (mentions you): сообщение 0"), std::string::npos);
    EXPECT_NE(text.find(std::string(300, 'x') + "…"), std::string::npos) << "cut at 300";
    EXPECT_EQ(text.find(std::string(301, 'x')), std::string::npos);
    EXPECT_NE(text.find("and 2 more"), std::string::npos);
    EXPECT_EQ(text.find("сообщение 11"), std::string::npos);
}

TEST(TelegramGateway, IdsAndTexts) {
    EXPECT_TRUE(Telegram::valid_id("1389231240"));
    EXPECT_TRUE(Telegram::valid_id("-1001234567890"));
    EXPECT_FALSE(Telegram::valid_id(""));
    EXPECT_FALSE(Telegram::valid_id("-"));
    EXPECT_FALSE(Telegram::valid_id("12a"));
    EXPECT_FALSE(Telegram::valid_id("../me"));
    EXPECT_EQ(Telegram::text_of(json{{"content", {{"type", "photo"}, {"caption", "кот"}}}}), "кот");
    EXPECT_EQ(Telegram::text_of(json{{"content", {{"type", "voice"}}}}), "[voice]");
    EXPECT_EQ(Telegram::encode("a b&c"), "a%20b%26c");
}

TEST(TelegramTools, RequestsOfTheTools) {
    std::string refusal;
    const json history = Agent::TelegramTools::request_of("tg_history", json{{"chat", "-100"}, {"limit", 5}}, refusal);
    EXPECT_TRUE(refusal.empty());
    EXPECT_EQ(history["path"], "/api/v1/telegram/chats/-100/messages");
    EXPECT_EQ(history["query"], json::array({json::array({"limit", "5"})}));

    const json react =
        Agent::TelegramTools::request_of("tg_react", json{{"chat", 10}, {"message", "55"}, {"emoji", "👍"}}, refusal);
    EXPECT_EQ(react["method"], "POST");
    EXPECT_EQ(react["path"], "/api/v1/telegram/chats/10/messages/55/reactions");
    EXPECT_EQ(react["body"]["emoji"], "👍");

    const json read =
        Agent::TelegramTools::request_of("tg_mark_read", json{{"chat", "10"}, {"messages", {"1", 2}}}, refusal);
    EXPECT_EQ(read["body"]["message_ids"], json::array({"1", "2"}));

    Agent::TelegramTools::request_of("tg_history", json{{"chat", "../../me"}}, refusal);
    EXPECT_NE(refusal.find("invalid"), std::string::npos) << "a path in an id is refused";
    refusal.clear();
    Agent::TelegramTools::request_of("tg_mark_read", json{{"chat", "10"}, {"messages", json::array()}}, refusal);
    EXPECT_FALSE(refusal.empty());
    EXPECT_FALSE(Agent::TelegramTools::is_telegram("tg_send")) << "sending is a proposal (Actions)";
}

TEST(TelegramPoll, WaitingIsAnIncomingMessageOverADayOld) {
    const json dm = {{"id", "10"}, {"title", "Anna"}, {"type", "private"}};
    const long long now = 2000000000;
    EXPECT_EQ(waiting_since(dm, json{{"is_outgoing", false}, {"date", now - 90000}}, now), now - 90000);
    EXPECT_EQ(waiting_since(dm, json{{"is_outgoing", false}, {"date", now - 3600}}, now), 0) << "less than a day";
    EXPECT_EQ(waiting_since(dm, json{{"is_outgoing", true}, {"date", now - 90000}}, now), 0) << "the owner answered";
    const json group = {{"id", "-1"}, {"type", "supergroup"}};
    EXPECT_EQ(waiting_since(group, json{{"is_outgoing", false}, {"date", now - 90000}}, now), 0);
}
