/**
 * @file test_agent_gateway.cpp
 * @brief One chat turn with an agent pod's gateway: a plain reply, a round of
 *        client tools sent back as the whole exchange, a long tool result cut
 *        on a character boundary, and the ways a turn fails.
 */

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/Gateway.hpp"
#include "net/Http.hpp"

namespace {

using json = nlohmann::json;
namespace G = Agent::Gateway;

/// Answers each request with the next scripted response.
struct Pod : Net::Http::Transport {
    std::vector<Net::Http::Response> replies;
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
            throw Net::Http::TransportError("connection refused");
        }
        if (sent.size() > replies.size()) {
            throw std::runtime_error("no reply scripted");
        }
        return replies[sent.size() - 1];
    }
};

Net::Http::Response words(const std::string& text) {
    return {200,
            json{{"choices", {{{"message", {{"role", "assistant"}, {"content", text}}}, {"finish_reason", "stop"}}}}}
                .dump()};
}

Net::Http::Response calls(const json& tool_calls) {
    return {200,
            json{{"choices",
                  {{{"message", {{"role", "assistant"}, {"content", ""}, {"tool_calls", tool_calls}}},
                    {"finish_reason", "tool_calls"}}}}}
                .dump()};
}

json call(const std::string& id, const std::string& name, const std::string& args) {
    return json{{"id", id}, {"type", "function"}, {"function", {{"name", name}, {"arguments", args}}}};
}

const json kTools = json::array({{{"type", "function"}, {"function", {{"name", "habits"}}}}});

G::Outcome talk(Pod& pod, const G::ToolRunner& run, const json& tools = kTools) {
    return G::converse(pod, "http://10.42.3.7:18789", "gw-token", "user-1", "Как дела?", tools, run);
}

G::ToolRunner no_tools() {
    return [](const std::string&, const json&) -> std::string { throw std::runtime_error("no tool expected"); };
}

}  // namespace

TEST(AgentGateway, APlainReplyEndsTheTurn) {
    Pod pod;
    pod.replies = {words("Привет!")};
    const auto out = talk(pod, no_tools());
    EXPECT_EQ(out.reply, "Привет!");
    EXPECT_TRUE(out.tools.empty());

    ASSERT_EQ(pod.sent.size(), 1U);
    const auto& r = pod.sent[0];
    EXPECT_EQ(r.method, "POST");
    EXPECT_EQ(r.url, "http://10.42.3.7:18789/v1/chat/completions");
    EXPECT_EQ(r.timeout_seconds, G::kCallTimeoutSeconds);
    ASSERT_EQ(r.headers.size(), 1U);
    EXPECT_EQ(r.headers[0].second, "Bearer gw-token");
    const auto body = json::parse(r.body);
    EXPECT_EQ(body["model"], "openclaw");
    EXPECT_EQ(body["user"], "user-1");
    EXPECT_EQ(body["messages"], json::parse(R"([{"role":"user","content":"Как дела?"}])"));
    EXPECT_EQ(body["tools"], kTools);
}

TEST(AgentGateway, ToolCallsAreRunAndTheWholeExchangeGoesBack) {
    Pod pod;
    pod.replies = {calls(json::array({call("c1", "habits", "{}"), call("c2", "goals", R"({"status":"done"})")})),
                   words("Две привычки и одна цель.")};
    std::vector<std::pair<std::string, json>> ran;
    const auto out = talk(pod, [&](const std::string& name, const json& args) {
        ran.emplace_back(name, args);
        return name == "habits" ? std::string(R"([{"name":"Зарядка"}])") : std::string(R"([])");
    });
    EXPECT_EQ(out.reply, "Две привычки и одна цель.");
    EXPECT_EQ(out.tools, (std::vector<std::string>{"habits", "goals"}));
    ASSERT_EQ(ran.size(), 2U);
    EXPECT_EQ(ran[1].second, json::parse(R"({"status":"done"})"));

    ASSERT_EQ(pod.sent.size(), 2U);
    const auto messages = json::parse(pod.sent[1].body)["messages"];
    ASSERT_EQ(messages.size(), 4U);
    EXPECT_EQ(messages[0]["role"], "user");
    EXPECT_EQ(messages[1]["role"], "assistant");
    EXPECT_EQ(messages[1]["tool_calls"].size(), 2U);
    EXPECT_EQ(messages[2], json::parse(R"({"role":"tool","tool_call_id":"c1","content":"[{\"name\":\"Зарядка\"}]"})"));
    EXPECT_EQ(messages[3]["tool_call_id"], "c2");
}

TEST(AgentGateway, ArgumentsThatDoNotParseBecomeNone) {
    Pod pod;
    pod.replies = {calls(json::array({call("c1", "today", "{not json")})), words("Готово.")};
    json seen;
    talk(pod, [&](const std::string&, const json& args) {
        seen = args;
        return std::string("{}");
    });
    EXPECT_EQ(seen, json::object());
}

TEST(AgentGateway, ALongToolResultIsCutOnACharacterBoundary) {
    Pod pod;
    pod.replies = {calls(json::array({call("c1", "today", "{}")})), words("Ок.")};
    // "ж" is two bytes; an odd start puts the limit inside one.
    const std::string long_result = "x" + std::string(G::kToolResultMax, 'a') + std::string(1000, 'b');
    std::string cyrillic = "x";
    while (cyrillic.size() < G::kToolResultMax + 100) {
        cyrillic += "ж";
    }
    for (const auto& result : {long_result, cyrillic}) {
        pod.sent.clear();
        talk(pod, [&](const std::string&, const json&) { return result; });
        const auto content = json::parse(pod.sent[1].body)["messages"][2]["content"].get<std::string>();
        EXPECT_LE(content.size(), G::kToolResultMax + std::string("…").size());
        EXPECT_EQ(content.substr(content.size() - std::string("…").size()), "…");
    }
}

TEST(AgentGateway, NoToolsMeansNoToolsField) {
    Pod pod;
    pod.replies = {words("Привет!")};
    talk(pod, no_tools(), json::array());
    EXPECT_FALSE(json::parse(pod.sent[0].body).contains("tools"));
}

TEST(AgentGateway, AGatewayThatDoesNotAnswerOrRefusesIsUnavailable) {
    Pod down;
    down.fail = true;
    try {
        talk(down, no_tools());
        FAIL() << "expected an error";
    } catch (const G::Error& e) {
        EXPECT_EQ(e.code, "unavailable");
    }
    Pod refusing;
    refusing.replies = {{502, "bad gateway"}};
    try {
        talk(refusing, no_tools());
        FAIL() << "expected an error";
    } catch (const G::Error& e) {
        EXPECT_EQ(e.code, "unavailable");
    }
}

TEST(AgentGateway, NoWordsOrEndlessToolsFail) {
    Pod silent;
    silent.replies = {words("")};
    try {
        talk(silent, no_tools());
        FAIL() << "expected an error";
    } catch (const G::Error& e) {
        EXPECT_EQ(e.code, "failed");
    }
    Pod junk;
    junk.replies = {{200, "<html>"}};
    EXPECT_THROW(talk(junk, no_tools()), G::Error);

    Pod looping;
    for (int i = 0; i < G::kMaxRounds; ++i) {
        looping.replies.push_back(calls(json::array({call("c" + std::to_string(i), "habits", "{}")})));
    }
    try {
        talk(looping, [](const std::string&, const json&) { return std::string("[]"); });
        FAIL() << "expected an error";
    } catch (const G::Error& e) {
        EXPECT_EQ(e.code, "failed");
    }
    EXPECT_EQ(looping.sent.size(), static_cast<std::size_t>(G::kMaxRounds));
}

TEST(AgentGateway, AnAgentStillStartingIsNotReadyOnTheFirstCallOnly) {
    const std::string starting =
        R"({"error":{"message":"AgentDatabaseAdmissionError: Agent main has not completed startup inspection"}})";
    Pod first;
    first.replies = {{500, starting}};
    try {
        talk(first, no_tools());
        FAIL() << "expected an error";
    } catch (const G::Error& e) {
        EXPECT_EQ(e.code, "unavailable");
        EXPECT_TRUE(e.not_ready);
    }

    // After a round of tools the model has been called: sending again could repeat it.
    Pod later;
    later.replies = {calls(json::array({call("c1", "habits", "{}")})), {500, starting}};
    try {
        talk(later, [](const std::string&, const json&) { return std::string("[]"); });
        FAIL() << "expected an error";
    } catch (const G::Error& e) {
        EXPECT_FALSE(e.not_ready);
    }

    Pod broken;
    broken.replies = {{500, R"({"error":{"message":"boom"}})"}};
    try {
        talk(broken, no_tools());
        FAIL() << "expected an error";
    } catch (const G::Error& e) {
        EXPECT_FALSE(e.not_ready);
    }
}
