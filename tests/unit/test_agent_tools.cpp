/**
 * @file test_agent_tools.cpp
 * @brief The Life OS tools of Ani's agent: the list the gateway gets, the API
 *        request each call becomes, what the model reads of an answer, and
 *        the runner's requests. And the access claims the tools' token shares
 *        with the login.
 */

#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

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
    EXPECT_EQ(
        names,
        (std::set<std::string>{"today", "tasks_of_day", "find_tasks", "goals", "habits", "sleep", "money", "food"}));
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
    EXPECT_NE(p.find("(today, tasks_of_day, find_tasks, goals, habits, sleep, money, food)"), std::string::npos);
    EXPECT_NE(p.find("never from memory"), std::string::npos);
    EXPECT_EQ(p.back(), ']');

    const auto bare = Agent::preface("", "UTC", json::array());
    EXPECT_EQ(bare, "[Life OS: Reply in one or two short spoken sentences.]");
}

TEST(AgentTools, AClearedChatIsANewSession) {
    EXPECT_EQ(Agent::session_of("u-1", 0), "u-1") << "the key before any clear stays as it was";
    EXPECT_EQ(Agent::session_of("u-1", 2), "u-1#2");
}
