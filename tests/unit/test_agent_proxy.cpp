/**
 * @file test_agent_proxy.cpp
 * @brief The agent pods' model proxy: who may call it, the monthly budget, the
 *        pinned model, the usage of JSON and SSE answers, and an upstream that
 *        does not answer.
 */

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/Proxy.hpp"
#include "agent/Settings.hpp"
#include "net/Http.hpp"
#include "utils/Crypto.hpp"

namespace {

using json = nlohmann::json;
namespace P = Agent::Proxy;

struct Upstream : Net::Http::Transport {
    Net::Http::Response reply{200, R"({"choices":[],"usage":{"prompt_tokens":12,"completion_tokens":3}})"};
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

Agent::Settings settings() {
    Agent::Settings s;
    s.model = "gpt-5-mini";
    s.upstream_url = "https://api.openai.com/v1";
    s.upstream_key = "sk-upstream";
    s.monthly_tokens = 1000;
    return s;
}

struct Book {
    long long spent = 0;
    std::vector<P::Usage> recorded;
    P::Hooks hooks() {
        P::Hooks h;
        h.owner_of = [](const std::string& hash) -> std::optional<std::string> {
            if (hash == Utils::Crypto::sha256_hex("good-token")) {
                return std::string("owner-1");
            }
            return std::nullopt;
        };
        h.month_tokens = [this](const std::string&) { return spent; };
        h.record = [this](const std::string& owner, const P::Usage& u) {
            EXPECT_EQ(owner, "owner-1");
            recorded.push_back(u);
        };
        return h;
    }
};

constexpr const char* kBody = R"({"model":"whatever","messages":[{"role":"user","content":"hi"}]})";

}  // namespace

TEST(AgentProxy, OnlyALiveRunsTokenGetsIn) {
    Upstream up;
    Book book;
    EXPECT_EQ(P::handle("", kBody, settings(), up, book.hooks()).status, 401) << "no token";
    EXPECT_EQ(P::handle("Bearer ", kBody, settings(), up, book.hooks()).status, 401) << "an empty token";
    EXPECT_EQ(P::handle("Bearer stale", kBody, settings(), up, book.hooks()).status, 401) << "a stopped run's token";
    EXPECT_TRUE(up.sent.empty());
}

TEST(AgentProxy, ForwardsWithTheWorkersKeyAndThePinnedModel) {
    Upstream up;
    Book book;
    const auto r = P::handle("Bearer good-token", kBody, settings(), up, book.hooks());
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(r.content_type, "application/json");
    ASSERT_EQ(up.sent.size(), 1U);
    const auto& sent = up.sent[0];
    EXPECT_EQ(sent.method, "POST");
    EXPECT_EQ(sent.url, "https://api.openai.com/v1/chat/completions");
    ASSERT_EQ(sent.headers.size(), 1U);
    EXPECT_EQ(sent.headers[0].second, "Bearer sk-upstream");
    EXPECT_EQ(json::parse(sent.body)["model"], "gpt-5-mini");
    ASSERT_EQ(book.recorded.size(), 1U);
    EXPECT_EQ(book.recorded[0].prompt_tokens, 12);
    EXPECT_EQ(book.recorded[0].completion_tokens, 3);
}

TEST(AgentProxy, AStreamAsksForUsageAndIsReplayedAsSse) {
    Upstream up;
    up.reply.body =
        "data: {\"choices\":[{\"delta\":{\"content\":\"Hi\"}}]}\n\n"
        "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":40,\"completion_tokens\":7}}\n\n"
        "data: [DONE]\n\n";
    Book book;
    const auto r = P::handle("Bearer good-token", R"({"stream":true,"messages":[]})", settings(), up, book.hooks());
    EXPECT_EQ(r.content_type, "text/event-stream");
    EXPECT_EQ(r.body, up.reply.body);
    EXPECT_TRUE(json::parse(up.sent[0].body)["stream_options"]["include_usage"].get<bool>());
    ASSERT_EQ(book.recorded.size(), 1U);
    EXPECT_TRUE(book.recorded[0].reported);
    EXPECT_EQ(book.recorded[0].prompt_tokens, 40);
}

TEST(AgentProxy, TheMonthlyBudgetStopsTheCalls) {
    Upstream up;
    Book book;
    book.spent = 1000;
    const auto r = P::handle("Bearer good-token", kBody, settings(), up, book.hooks());
    EXPECT_EQ(r.status, 429);
    EXPECT_EQ(json::parse(r.body)["error"]["type"], "insufficient_quota");
    EXPECT_TRUE(up.sent.empty());
}

TEST(AgentProxy, BadBodiesAndDeadUpstreams) {
    Upstream up;
    Book book;
    EXPECT_EQ(P::handle("Bearer good-token", "not json", settings(), up, book.hooks()).status, 400);
    up.fail = true;
    EXPECT_EQ(P::handle("Bearer good-token", kBody, settings(), up, book.hooks()).status, 502);
    up.fail = false;
    up.reply = {429, R"({"error":{"message":"rate limited"}})"};
    const auto r = P::handle("Bearer good-token", kBody, settings(), up, book.hooks());
    EXPECT_EQ(r.status, 429) << "the upstream's own answer goes back";
    EXPECT_TRUE(book.recorded.empty()) << "a refused call costs nothing";
}

TEST(AgentProxy, UsageOfAnswersWithout) {
    EXPECT_FALSE(P::usage_of("{}", false).reported);
    EXPECT_FALSE(P::usage_of("data: [DONE]\n", true).reported);
    EXPECT_FALSE(P::usage_of(R"({"usage":null})", false).reported);
    const auto u = P::usage_of(R"({"usage":{"prompt_tokens":null,"completion_tokens":5}})", false);
    EXPECT_TRUE(u.reported);
    EXPECT_EQ(u.prompt_tokens, 0);
    EXPECT_EQ(u.completion_tokens, 5);
}
