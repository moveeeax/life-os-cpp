/**
 * @file test_agent_kube.cpp
 * @brief The Kubernetes calls of the agent against a scripted API server: the
 *        paths, methods and credentials, and how 404, 409 and refusals read.
 */

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/Kube.hpp"
#include "net/Http.hpp"

namespace {

using json = nlohmann::json;

struct ApiServer : Net::Http::Transport {
    std::vector<Net::Http::Response> replies;
    std::vector<Net::Http::Request> sent;
    bool fail = false;

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
        auto reply = replies.front();
        replies.erase(replies.begin());
        return reply;
    }
};

Agent::Kube::Client client(ApiServer& api) {
    return Agent::Kube::Client(api, "https://kubernetes.default.svc", "sa-token", "/sa/ca.crt");
}

}  // namespace

TEST(AgentKube, CreateSendsTheObjectWithTheServiceAccount) {
    ApiServer api;
    api.replies = {{201, "{}"}, {409, R"({"message":"pods \"ani-x\" already exists"})"}};
    auto c = client(api);
    EXPECT_TRUE(c.create("life-os-agents", "pods", json{{"kind", "Pod"}}));
    EXPECT_FALSE(c.create("life-os-agents", "pods", json{{"kind", "Pod"}})) << "409: one of that name exists";
    const auto& r = api.sent[0];
    EXPECT_EQ(r.method, "POST");
    EXPECT_EQ(r.url, "https://kubernetes.default.svc/api/v1/namespaces/life-os-agents/pods");
    EXPECT_EQ(r.content_type, "application/json");
    EXPECT_EQ(r.ca_file, "/sa/ca.crt");
    EXPECT_EQ(r.headers[0].second, "Bearer sa-token");
    EXPECT_EQ(json::parse(r.body)["kind"], "Pod");
}

TEST(AgentKube, GetReadsAMissingObjectAsNone) {
    ApiServer api;
    api.replies = {{404, "{}"}, {200, R"({"metadata":{"name":"ani-x"}})"}};
    auto c = client(api);
    EXPECT_FALSE(c.get("ns", "pods", "ani-x").has_value());
    EXPECT_EQ((*c.get("ns", "pods", "ani-x"))["metadata"]["name"], "ani-x");
    EXPECT_EQ(api.sent[0].url, "https://kubernetes.default.svc/api/v1/namespaces/ns/pods/ani-x");
}

TEST(AgentKube, ListSelectsByLabel) {
    ApiServer api;
    api.replies = {{200, R"({"items":[{"metadata":{"name":"a"}},{"metadata":{"name":"b"}}]})"}};
    auto c = client(api);
    const auto items = c.list("ns", "pods", "life-os.tarassov.me/managed-by=life-os-worker");
    EXPECT_EQ(items.size(), 2U);
    EXPECT_EQ(api.sent[0].url,
              "https://kubernetes.default.svc/api/v1/namespaces/ns/pods?labelSelector="
              "life-os.tarassov.me%2Fmanaged-by%3Dlife-os-worker");
}

TEST(AgentKube, RemoveIsFineWithAMissingObjectAndCanCutTheGrace) {
    ApiServer api;
    api.replies = {{200, "{}"}, {404, "{}"}, {200, "{}"}};
    auto c = client(api);
    c.remove("ns", "pods", "ani-x");
    c.remove("ns", "pods", "ani-x");
    c.remove("ns", "pods", "ani-x", 0);
    EXPECT_EQ(api.sent[0].method, "DELETE");
    EXPECT_TRUE(api.sent[0].body.empty()) << "the pod's own grace period";
    EXPECT_EQ(json::parse(api.sent[2].body)["gracePeriodSeconds"], 0);
}

TEST(AgentKube, RefusalsAndSilenceThrow) {
    ApiServer api;
    api.replies = {{403, R"({"message":"pods is forbidden"})"}};
    auto c = client(api);
    try {
        c.create("ns", "pods", json::object());
        FAIL() << "a refusal throws";
    } catch (const Agent::Kube::Error& e) {
        EXPECT_EQ(e.status, 403);
        EXPECT_NE(std::string(e.what()).find("pods is forbidden"), std::string::npos);
    }
    api.fail = true;
    try {
        c.get("ns", "pods", "ani-x");
        FAIL() << "no answer throws";
    } catch (const Agent::Kube::Error& e) {
        EXPECT_EQ(e.status, 0);
    }
}
