/**
 * @file test_agent_tools.cpp
 * @brief The Life OS tools of Ani's agent: the list the gateway gets, the API
 *        request each call becomes, what the model reads of an answer, and
 *        the runner's requests. And the access claims the tools' token shares
 *        with the login. And the write tools: what a call proposes, how a
 *        task or a habit is found, and how a confirmed change is sent.
 */

#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/Actions.hpp"
#include "agent/CalendarTools.hpp"
#include "agent/Tools.hpp"
#include "agent/Turn.hpp"
#include "net/Http.hpp"
#include "security/Jwt.hpp"

namespace {

using json = nlohmann::json;
namespace T = Agent::Tools;
using Query = std::vector<std::pair<std::string, std::string>>;

struct Api : Net::Http::Transport {
    Net::Http::Response reply{200, R"({"data":[{"id":"h1","name":"Зарядка","note":null}]})"};
    bool fail = false;
    std::vector<Net::Http::Request> sent;

    Net::Http::Response get(const std::string&, const std::vector<std::pair<std::string, std::string>>&) override {
        throw std::runtime_error("not expected");
    }
    Net::Http::Response post_json(const std::string&,
                                  const std::string&,
                                  const std::vector<std::pair<std::string, std::string>>&,
                                  long) override {
        throw std::runtime_error("not expected");
    }
    Net::Http::Response send(const Net::Http::Request& r) override {
        sent.push_back(r);
        if (fail) {
            throw Net::Http::TransportError("timed out");
        }
        return reply;
    }
};

}  // namespace

TEST(AgentTools, EveryDefinedToolHasARoute) {
    const auto defs = T::definitions();
    std::set<std::string> names;
    for (const auto& d : defs) {
        EXPECT_EQ(d["type"], "function");
        const std::string name = d["function"]["name"];
        names.insert(name);
        EXPECT_FALSE(d["function"]["description"].get<std::string>().empty()) << name;
        EXPECT_EQ(d["function"]["parameters"]["type"], "object") << name;
        EXPECT_TRUE(T::route(name, json::object(), "UTC").has_value()) << name;
    }
    EXPECT_EQ(names,
              (std::set<std::string>{
                  "today", "tasks_of_day", "find_tasks", "goals", "habits", "notes", "sleep", "money", "food"}));
    EXPECT_FALSE(T::route("delete_everything", json::object(), "UTC").has_value());
}

TEST(AgentTools, CallsBecomeReadsInTheUsersZone) {
    auto c = T::route("tasks_of_day", json{{"date", "2026-10-26"}}, "Asia/Almaty");
    EXPECT_EQ(c->path, "/api/v1/tasks/agenda");
    EXPECT_EQ(c->query, (Query{{"date", "2026-10-26"}, {"tz", "Asia/Almaty"}}));

    c = T::route("goals", json::object(), "UTC");
    EXPECT_EQ(c->path, "/api/v1/goals");
    EXPECT_EQ(c->query, (Query{{"status", "active"}, {"tz", "UTC"}}));

    c = T::route("find_tasks", json{{"query", "паспорт"}, {"status", "open"}}, "UTC");
    EXPECT_EQ(c->path, "/api/v1/tasks/items");
    EXPECT_EQ(c->query, (Query{{"limit", "20"}, {"q", "паспорт"}, {"status", "open"}, {"tz", "UTC"}}));

    c = T::route("money", json::object(), "UTC");
    EXPECT_EQ(c->path, "/api/v1/money/reports/period");
    EXPECT_EQ(c->query, (Query{{"kind", "month"}, {"tz", "UTC"}}));

    c = T::route("sleep", json{{"from", "2026-10-19"}, {"to", "2026-10-25"}}, "UTC");
    EXPECT_EQ(c->path, "/api/v1/fitness/sleep");
    EXPECT_EQ(c->query, (Query{{"from", "2026-10-19"}, {"to", "2026-10-25"}, {"tz", "UTC"}}));
}

TEST(AgentTools, ArgumentsOfTheWrongShapeAreDropped) {
    auto c = T::route("today", json{{"date", "tomorrow"}}, "UTC");
    EXPECT_EQ(c->query, (Query{{"tz", "UTC"}}));
    c = T::route("food", json{{"date", 20261026}}, "UTC");
    EXPECT_EQ(c->query, (Query{{"tz", "UTC"}}));
    c = T::route("habits", json::array({1, 2}), "UTC");
    EXPECT_EQ(c->path, "/api/v1/habits");
}

TEST(AgentTools, CompactDropsIdsAndEmptiness) {
    const auto in = json::parse(R"({
        "id": "t1", "goal_id": "g1", "title": "Паспорт", "note": null, "tags": [], "errors": {"x": 1},
        "valid": true, "items": [{"id": "i1", "name": "a", "sub": {"owner_id": "u", "n": 0}}]
    })");
    EXPECT_EQ(T::compact(in), json::parse(R"({"title":"Паспорт","valid":true,"items":[{"name":"a","sub":{"n":0}}]})"));
}

TEST(AgentTools, WhatTheModelReadsOfAnAnswer) {
    EXPECT_EQ(T::result_text({200, R"({"data":{"id":"x","total":3}})"}), R"({"total":3})");
    EXPECT_EQ(json::parse(T::result_text({404, R"({"error":"not_found"})"}))["error"], "not_available");
    const auto bad =
        json::parse(T::result_text({400, R"({"error":"invalid_range","status":400,"message":"from and to"})"}));
    EXPECT_EQ(bad["error"], "invalid_range");
    EXPECT_EQ(bad["status"], 400);
    EXPECT_EQ(bad["message"], "from and to");
    EXPECT_EQ(json::parse(T::result_text({500, "oops"}))["error"], "failed");
}

TEST(AgentTools, TheRunnerReadsTheApiAsTheUser) {
    Api api;
    const auto run = T::runner(api, "http://life-os-cpp:8080", "jwt-1", "Asia/Almaty");
    EXPECT_EQ(run("habits", json::object()), R"([{"name":"Зарядка"}])");
    ASSERT_EQ(api.sent.size(), 1U);
    EXPECT_EQ(api.sent[0].method, "GET");
    EXPECT_EQ(api.sent[0].url, "http://life-os-cpp:8080/api/v1/habits?tz=Asia/Almaty");
    EXPECT_EQ(api.sent[0].headers[0], (std::pair<std::string, std::string>{"Authorization", "Bearer jwt-1"}));

    run("find_tasks", json{{"query", "a&b c"}});
    EXPECT_EQ(api.sent[1].url, "http://life-os-cpp:8080/api/v1/tasks/items?limit=20&q=a%26b%20c&tz=Asia/Almaty");

    EXPECT_EQ(json::parse(run("rm", json::object()))["error"], "unknown_tool");
    EXPECT_EQ(api.sent.size(), 2U) << "an unknown tool calls nothing";

    api.fail = true;
    EXPECT_EQ(json::parse(run("habits", json::object()))["error"], "unavailable");
}

TEST(AgentTools, AccessClaimsAreTheLoginsSet) {
    Security::Auth::AccessSubject who;
    who.id = "u-1";
    who.confirmed = true;
    who.permissions = 6;
    who.roles = {"User"};
    const auto c = Security::Auth::access_claims(who, 1'700'000'000'123, 300, "roles", "life-os", "web");
    EXPECT_EQ(c["sub"], "u-1");
    EXPECT_EQ(c["iat"], 1'700'000'000);
    EXPECT_EQ(c["iat_ms"], 1'700'000'000'123);
    EXPECT_EQ(c["exp"], 1'700'000'300);
    EXPECT_EQ(c["typ"], "access");
    EXPECT_EQ(c["confirmed"], true);
    EXPECT_EQ(c["permissions"], 6);
    EXPECT_EQ(c["roles"], json::array({"User"}));
    EXPECT_EQ(c["iss"], "life-os");
    EXPECT_EQ(c["aud"], "web");

    Security::Auth::AccessSubject nobody;
    nobody.id = "u-2";
    const auto bare = Security::Auth::access_claims(nobody, 0, 60, "roles", "", "");
    EXPECT_EQ(bare["roles"], json::array());
    EXPECT_FALSE(bare.contains("iss"));
    EXPECT_FALSE(bare.contains("aud"));
}

TEST(AgentTools, ThePrefaceSendsTheModelToTheTools) {
    const auto p = Agent::preface("2026-10-26 00:05, Mon", "Asia/Almaty", T::definitions());
    EXPECT_EQ(p.rfind("[Life OS: the person's local time is 2026-10-26 00:05, Mon, Asia/Almaty. ", 0), 0U);
    EXPECT_NE(p.find("one or two short spoken sentences"), std::string::npos);
    EXPECT_NE(p.find("(today, tasks_of_day, find_tasks, goals, habits, notes, sleep, money, food)"), std::string::npos);
    EXPECT_NE(p.find("never from memory"), std::string::npos);
    EXPECT_EQ(p.back(), ']');

    const auto bare = Agent::preface("", "UTC", json::array());
    EXPECT_EQ(bare, "[Life OS: Reply in one or two short spoken sentences.]");
}

TEST(AgentTools, ThePrefaceTellsOfProposalsAndWhatBecameOfThem) {
    json tools = T::definitions();
    for (const auto& t : Agent::Actions::definitions()) {
        tools.push_back(t);
    }
    const auto p = Agent::preface(
        "2026-10-26 00:05, Mon", "UTC", tools, json::parse(R"([{"summary":"Add the task «Молоко»","status":"done"},
                                                  {"summary":"Tick «Зарядка» for today","status":"cancelled"}])"));
    EXPECT_NE(p.find("(today, tasks_of_day, find_tasks, goals, habits, notes, sleep, money, food)"), std::string::npos)
        << "the write tools are not in the read list";
    EXPECT_NE(p.find("Changes (create_task, complete_task, check_habit, add_note, journal_note) are only proposed"),
              std::string::npos);
    EXPECT_NE(p.find("Add the task «Молоко»: done. Tick «Зарядка» for today: cancelled by the person."),
              std::string::npos);
    EXPECT_EQ(p.back(), ']');
}

namespace {

namespace A = Agent::Actions;

/// Answers each GET by its path; records every request.
struct Site : Net::Http::Transport {
    std::vector<std::pair<std::string, Net::Http::Response>> answers;
    std::vector<Net::Http::Request> sent;
    Net::Http::Response get(const std::string&, const std::vector<std::pair<std::string, std::string>>&) override {
        throw std::runtime_error("not expected");
    }
    Net::Http::Response post_json(const std::string&,
                                  const std::string&,
                                  const std::vector<std::pair<std::string, std::string>>&,
                                  long) override {
        throw std::runtime_error("not expected");
    }
    Net::Http::Response send(const Net::Http::Request& r) override {
        sent.push_back(r);
        for (const auto& [prefix, res] : answers) {
            if (r.url.rfind(prefix, 0) == 0) {
                return res;
            }
        }
        return {404, R"({"error":"not_found"})"};
    }
};

const std::string kApi = "http://api:8080";

A::Proposed propose(Site& site, const std::string& name, const json& args) {
    return A::propose(site, kApi, "jwt", "Asia/Almaty", "2026-10-26", name, args);
}

json refused(const A::Proposed& p) {
    EXPECT_TRUE(std::holds_alternative<std::string>(p));
    return std::holds_alternative<std::string>(p) ? json::parse(std::get<std::string>(p)) : json();
}

}  // namespace

TEST(AgentActions, EveryWriteToolIsKnown) {
    std::set<std::string> names;
    for (const auto& d : A::definitions()) {
        names.insert(d["function"]["name"].get<std::string>());
        EXPECT_FALSE(d["function"]["parameters"]["required"].empty());
    }
    EXPECT_EQ(names,
              (std::set<std::string>{"create_task", "complete_task", "check_habit", "add_note", "journal_note"}));
    for (const auto& n : names) {
        EXPECT_TRUE(A::is_write(n)) << n;
    }
    EXPECT_FALSE(A::is_write("habits"));
}

TEST(AgentActions, ANewTaskAndANoteNeedNoLookup) {
    Site site;
    auto p = std::get<A::Proposal>(
        propose(site, "create_task", json{{"title", "  Купить молоко "}, {"area", "health"}, {"due", "2026-10-27"}}));
    EXPECT_EQ(p.kind, "create_task");
    EXPECT_EQ(p.summary, "Add the task «Купить молоко», due 2026-10-27");
    EXPECT_EQ(p.request["method"], "POST");
    EXPECT_EQ(p.request["path"], "/api/v1/tasks/items");
    EXPECT_EQ(p.request["body"], json::parse(R"({"title":"Купить молоко","area":"health","due":"2026-10-27"})"));

    EXPECT_EQ(refused(propose(site, "create_task", json{{"title", "x"}, {"area", "work"}}))["error"], "invalid");
    EXPECT_EQ(refused(propose(site, "create_task", json{{"area", "health"}}))["error"], "invalid");

    p = std::get<A::Proposal>(propose(site, "add_note", json{{"text", "Позвонить маме"}}));
    EXPECT_EQ(p.summary, "Note to the inbox: «Позвонить маме»");
    EXPECT_EQ(p.request["body"], (json{{"text", "Позвонить маме"}}));
    EXPECT_TRUE(site.sent.empty());
}

TEST(AgentActions, ATaskIsClosedOnlyWhenItsWordsFindOneOpenTask) {
    Site site;
    site.answers = {{kApi + "/api/v1/tasks/items", {200, R"({"data":[{"id":"t1","title":"Паспорт","status":"open"},
                                       {"id":"t2","title":"Паспорт сыну","status":"in_progress"},
                                       {"id":"t3","title":"Паспорт старый","status":"done"}]})"}}};
    const auto many = refused(propose(site, "complete_task", json{{"task", "Пасп"}}));
    EXPECT_EQ(many["error"], "ambiguous");
    EXPECT_EQ(many["matches"], json::array({"Паспорт", "Паспорт сыну"})) << "a done task is not offered";
    EXPECT_EQ(site.sent[0].url, kApi + "/api/v1/tasks/items?q=%D0%9F%D0%B0%D1%81%D0%BF&limit=10&tz=Asia/Almaty");
    EXPECT_EQ(site.sent[0].headers[0], (std::pair<std::string, std::string>{"Authorization", "Bearer jwt"}));

    const auto p = std::get<A::Proposal>(propose(site, "complete_task", json{{"task", "Паспорт"}}));
    EXPECT_EQ(p.summary, "Close the task «Паспорт»") << "the exact title wins";
    EXPECT_EQ(p.request["method"], "PATCH");
    EXPECT_EQ(p.request["path"], "/api/v1/tasks/items/t1");
    EXPECT_EQ(p.request["body"], (json{{"status", "done"}}));

    site.answers = {{kApi + "/api/v1/tasks/items", {200, R"({"data":[]})"}}};
    EXPECT_EQ(refused(propose(site, "complete_task", json{{"task", "Визa"}}))["error"], "not_found");
}

TEST(AgentActions, AHabitIsFoundByItsNameAndTickedByHandOnly) {
    Site site;
    site.answers = {
        {kApi + "/api/v1/habits", {200, R"({"data":[{"id":"h1","name":"Зарядка","source":"manual","unit":""},
                                       {"id":"h2","name":"Вода","source":"manual","unit":"л"},
                                       {"id":"h3","name":"Шаги","source":"fitness:steps","unit":"steps"}]})"}}};
    auto p = std::get<A::Proposal>(propose(site, "check_habit", json{{"habit", "Зарядка"}}));
    EXPECT_EQ(p.summary, "Tick «Зарядка» for today");
    EXPECT_EQ(p.request["method"], "PUT");
    EXPECT_EQ(p.request["path"], "/api/v1/habits/h1/checks/2026-10-26");
    EXPECT_EQ(p.request["query"], json::parse(R"([["tz","Asia/Almaty"]])"));

    p = std::get<A::Proposal>(
        propose(site, "check_habit", json{{"habit", "Вод"}, {"amount", 1.5}, {"date", "2026-10-25"}}));
    EXPECT_EQ(p.summary, "Add 1.5 л to «Вода» for 2026-10-25");
    EXPECT_EQ(p.request["method"], "POST");
    EXPECT_EQ(p.request["path"], "/api/v1/habits/h2/checks/2026-10-25/add");
    EXPECT_EQ(p.request["body"], (json{{"value", 1.5}}));

    EXPECT_EQ(refused(propose(site, "check_habit", json{{"habit", "Шаги"}}))["error"], "automatic");
    const auto none = refused(propose(site, "check_habit", json{{"habit", "Йога"}}));
    EXPECT_EQ(none["error"], "not_found");
    EXPECT_EQ(none["habits"].size(), 3U);
    EXPECT_EQ(refused(propose(site, "check_habit", json{{"habit", "Зарядка"}, {"date", "2026-10-27"}}))["error"],
              "invalid")
        << "not a day ahead";
}

TEST(AgentActions, AConfirmedChangeIsSentAsTheUser) {
    Site site;
    site.answers = {{kApi + "/api/v1/tasks/notes", {201, R"({"data":{"id":"n1"}})"}},
                    {kApi + "/api/v1/tasks/items/t1", {422, R"({"error":"invalid_status","status":422})"}}};
    EXPECT_EQ(
        A::execute(
            site, kApi, "jwt", json{{"method", "POST"}, {"path", "/api/v1/tasks/notes"}, {"body", {{"text", "x"}}}}),
        "");
    EXPECT_EQ(site.sent[0].method, "POST");
    EXPECT_EQ(site.sent[0].body, R"({"text":"x"})");
    EXPECT_EQ(site.sent[0].content_type, "application/json");
    EXPECT_EQ(A::execute(site,
                         kApi,
                         "jwt",
                         json{{"method", "PATCH"},
                              {"path", "/api/v1/tasks/items/t1"},
                              {"query", json::parse(R"([["tz","UTC"]])")},
                              {"body", {{"status", "done"}}}}),
              "invalid_status");
    EXPECT_EQ(site.sent[1].url, kApi + "/api/v1/tasks/items/t1?tz=UTC");
    EXPECT_EQ(A::execute(site, kApi, "jwt", json{{"method", "DELETE"}, {"path", "/nope"}}), "not_found");

    const auto told = json::parse(A::proposed_text(A::Proposal{"add_note", "Note to the inbox: «x»", json()}));
    EXPECT_EQ(told["proposed"], "Note to the inbox: «x»");
}

TEST(AgentTools, ARepeatedPrefaceIsCutFromTheReply) {
    EXPECT_EQ(Agent::without_preface("[Life OS: the person's local time is 10:51.]\n\nПривет!"), "Привет!");
    EXPECT_EQ(Agent::without_preface("Привет! [Life OS: x]"), "Привет! [Life OS: x]");
    EXPECT_EQ(Agent::without_preface("[Life OS: nothing after]"), "[Life OS: nothing after]") << "never empty";
    EXPECT_EQ(Agent::without_preface("Просто ответ"), "Просто ответ");
}

TEST(AgentTools, AClearedChatIsANewSession) {
    EXPECT_EQ(Agent::session_of("u-1", 0), "u-1") << "the key before any clear stays as it was";
    EXPECT_EQ(Agent::session_of("u-1", 2), "u-1#2");
}

TEST(AgentActions, TelegramMessagesAreProposalsWithTheChatTitle) {
    for (const auto& d : A::telegram_definitions()) {
        EXPECT_TRUE(A::is_write(d["function"]["name"].get<std::string>()));
    }
    Site site;
    site.answers.emplace_back(kApi + "/api/v1/telegram/chats/10",
                              Net::Http::Response{200, R"({"data":{"id":"10","title":"Anna","type":"private"}})"});
    auto send = std::get<A::Proposal>(
        propose(site, "tg_send", json{{"chat", "10"}, {"text", "Буду в семь"}, {"reply_to", 55}}));
    EXPECT_EQ(send.kind, "tg_send");
    EXPECT_EQ(send.summary, "Send to Anna: «Буду в семь»");
    EXPECT_EQ(send.request["method"], "POST");
    EXPECT_EQ(send.request["path"], "/api/v1/telegram/chats/10/messages");
    EXPECT_EQ(send.request["body"]["reply_to"], "55");
    EXPECT_EQ(send.request["label"], "Send to Anna: ");
    EXPECT_EQ(send.request["body"]["key"].get<std::string>().rfind("ani-", 0), 0u) << "a retry sends once";
    EXPECT_EQ(site.sent.size(), 1u) << "only the chat was read; nothing sent";

    auto edit = std::get<A::Proposal>(
        propose(site, "tg_edit", json{{"chat", "10"}, {"message", "77"}, {"text", "Буду в восемь"}}));
    EXPECT_EQ(edit.request["method"], "PATCH");
    EXPECT_EQ(edit.request["path"], "/api/v1/telegram/chats/10/messages/77");
    EXPECT_EQ(edit.summary, "Edit your message in Anna to «Буду в восемь»");

    EXPECT_EQ(refused(propose(site, "tg_send", json{{"chat", "20"}, {"text", "x"}}))["error"], "not_available")
        << "an unknown chat, or Telegram not the person's";
    EXPECT_EQ(refused(propose(site, "tg_send", json{{"chat", "../x"}, {"text", "x"}}))["error"], "invalid");
    EXPECT_EQ(refused(propose(site, "tg_send", json{{"chat", "10"}, {"text", " "}}))["error"], "invalid");
}

TEST(AgentActions, TheDiaryIsAJournalNoteOfTheDay) {
    Site site;
    auto entry = std::get<A::Proposal>(
        propose(site, "journal_note", json{{"title", "Тихий день"}, {"body", "Сходил в зал, закрыл визу."}}));
    EXPECT_EQ(entry.kind, "journal_note");
    EXPECT_EQ(entry.request["path"], "/api/v1/notes");
    EXPECT_EQ(entry.request["body"]["topic"], "journal");
    EXPECT_EQ(entry.request["body"]["date"], "2026-10-26");
    EXPECT_TRUE(site.sent.empty());
    EXPECT_EQ(refused(propose(site, "journal_note", json{{"title", "x"}}))["error"], "invalid");
}

TEST(AgentActions, CalendarChangesAreProposalsInThePersonsTime) {
    for (const auto& d : A::calendar_definitions()) {
        EXPECT_TRUE(A::is_write(d["function"]["name"].get<std::string>()));
    }
    Site site;
    auto add = std::get<A::Proposal>(propose(
        site,
        "calendar_add",
        json{{"title", "Focus"}, {"start", "2026-10-29T14:00:00+05:00"}, {"end", "2026-10-29T15:30:00+05:00"}}));
    EXPECT_EQ(add.summary, "Add «Focus» to the calendar, 2026-10-29 14:00–15:30");
    EXPECT_EQ(add.request["method"], "POST");
    EXPECT_EQ(add.request["path"], "/api/v1/calendar/events");
    EXPECT_TRUE(site.sent.empty()) << "adding reads nothing";
    // Moving and removing name the event as the calendar has it, in the person's time, whatever Ani called it.
    site.answers = {
        {kApi + "/api/v1/calendar/events/abc_123",
         {200,
          R"({"data":{"id":"abc_123","title":"Lunch","start":"2026-10-30T08:00:00Z","end":"2026-10-30T09:00:00Z",)"
          R"("all_day":false,"location":"","start_local":"2026-10-30T13:00:00+05:00","end_local":"2026-10-30T14:00:00+05:00"}})"}}};
    auto move = std::get<A::Proposal>(propose(site,
                                              "calendar_move",
                                              json{{"event", "abc_123"},
                                                   {"title", "Call"},
                                                   {"start", "2026-10-30T04:00:00Z"},
                                                   {"end", "2026-10-30T05:00:00Z"}}));
    EXPECT_EQ(move.summary, "Move «Lunch» (2026-10-30 13:00–14:00) to 2026-10-30 09:00–10:00");
    EXPECT_EQ(move.request["path"], "/api/v1/calendar/events/abc_123");
    auto remove =
        std::get<A::Proposal>(propose(site, "calendar_remove", json{{"event", "abc_123"}, {"title", "Call"}}));
    EXPECT_EQ(remove.summary, "Remove «Lunch» (2026-10-30 13:00–14:00) from the calendar");
    EXPECT_EQ(remove.request["method"], "DELETE");
    for (const auto& r : site.sent) {
        EXPECT_EQ(r.method, "GET") << "proposing only reads";
    }
    EXPECT_EQ(refused(propose(site, "calendar_remove", json{{"event", "gone_1"}}))["error"], "not_available")
        << "an event the calendar does not have";

    EXPECT_EQ(
        refused(propose(site,
                        "calendar_add",
                        json{{"title", "x"}, {"start", "2026-10-29 14:00"}, {"end", "2026-10-29T15:00:00Z"}}))["error"],
        "invalid")
        << "a time without its offset";
    EXPECT_EQ(refused(propose(
                  site,
                  "calendar_add",
                  json{{"title", "x"}, {"start", "2026-10-29T15:00:00Z"}, {"end", "2026-10-29T14:00:00Z"}}))["error"],
              "invalid");
    EXPECT_EQ(refused(propose(site, "calendar_remove", json{{"event", "../x"}}))["error"], "invalid");
}

TEST(AgentTools, CalendarReadsGoThroughTheSite) {
    std::string refusal;
    const json events = Agent::CalendarTools::request_of(
        "calendar_events", json{{"from", "2026-10-29"}, {"to", "2026-10-31"}}, "Asia/Tashkent", refusal);
    EXPECT_TRUE(refusal.empty());
    EXPECT_EQ(events["path"], "/api/v1/calendar/events");
    EXPECT_EQ(events["query"][2], json::array({"tz", "Asia/Tashkent"}));
    Agent::CalendarTools::request_of(
        "calendar_events", json{{"from", "2026-10-31"}, {"to", "2026-10-29"}}, "UTC", refusal);
    EXPECT_FALSE(refusal.empty());
}
