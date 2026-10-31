/**
 * @file test_agent_manifest.cpp
 * @brief What the worker creates for one run of a user's agent: the names, the
 *        OpenClaw config (the proxy as the only model, the denied tools), the
 *        Secret and the Pod (restore, keeper, gateway; restricted security;
 *        worker nodes only).
 */

#include <algorithm>
#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/Manifest.hpp"
#include "agent/Settings.hpp"
#include "agent/Turn.hpp"

namespace {

using json = nlohmann::json;
namespace M = Agent::Manifest;

constexpr const char* kOwner = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa1";

Agent::Settings settings() {
    Agent::Settings s;
    s.image = "ghcr.io/openclaw/openclaw:2026.10.7-slim";
    s.keeper_image = "curlimages/curl:8.17.0";
    s.ns = "life-os-agents";
    s.node_label = "node-role.kubernetes.io/worker";
    s.model = "gpt-5-mini";
    s.upstream_url = "https://api.openai.com/v1";
    s.upstream_key = "sk-upstream";
    s.proxy_url = "http://life-os-cpp-worker.life-os.svc.cluster.local:9091/ani/llm/v1";
    return s;
}

M::Run run() {
    M::Run r;
    r.owner = kOwner;
    r.gateway_token = "gw-token";
    r.proxy_token = "proxy-token";
    r.get_url = "https://s3/get";
    r.put_url = "https://s3/put";
    return r;
}

const json& container(const json& list, const std::string& name) {
    for (const auto& c : list) {
        if (c.at("name") == name) {
            return c;
        }
    }
    static const json none;
    ADD_FAILURE() << "no container " << name;
    return none;
}

}  // namespace

TEST(AgentManifest, NamesComeFromTheOwnerHash) {
    const std::string hash = M::owner_hash(kOwner);
    EXPECT_EQ(hash.size(), 12U);
    EXPECT_EQ(M::pod_name(kOwner), "ani-" + hash);
    EXPECT_NE(M::pod_name(kOwner), M::pod_name("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb1"));
    EXPECT_EQ(M::pod_name(kOwner).find(kOwner), std::string::npos) << "the user id stays out of the cluster";
    EXPECT_EQ(M::snapshot_name(3), "state-3.tgz");
}

TEST(AgentManifest, TheOnlyModelIsTheProxyAndNoProviderKeyIsInside) {
    const auto s = settings();
    const json c = M::openclaw_config(s, "proxy-token");
    EXPECT_EQ(c["models"]["mode"], "replace");
    const auto& provider = c["models"]["providers"]["lifeos"];
    EXPECT_EQ(provider["baseUrl"], s.proxy_url);
    EXPECT_EQ(provider["apiKey"], "proxy-token");
    EXPECT_EQ(provider["api"], "openai-completions");
    EXPECT_EQ(provider["models"][0]["id"], "gpt-5-mini");
    EXPECT_EQ(c["agents"]["defaults"]["model"], "lifeos/gpt-5-mini");
    EXPECT_EQ(c["agents"]["defaults"]["models"]["lifeos/gpt-5-mini"]["agentRuntime"]["id"], "openclaw")
        << "the Codex runtime would drop the site's client tools";
    EXPECT_TRUE(c["agents"]["defaults"]["skipBootstrap"].get<bool>());
    EXPECT_EQ(c.dump().find("sk-upstream"), std::string::npos);

    EXPECT_EQ(c["gateway"]["bind"], "lan");
    EXPECT_EQ(c["gateway"]["auth"]["mode"], "token");
    EXPECT_FALSE(c["gateway"]["controlUi"]["enabled"].get<bool>());
    EXPECT_TRUE(c["gateway"]["http"]["endpoints"]["chatCompletions"]["enabled"].get<bool>());
    const auto& deny = c["tools"]["deny"];
    for (const char* group : {"group:runtime", "group:web", "group:automation", "group:nodes", "group:messaging"}) {
        EXPECT_NE(std::find(deny.begin(), deny.end(), group), deny.end()) << group;
    }
    EXPECT_FALSE(c["cron"]["enabled"].get<bool>());
    EXPECT_FALSE(c["tools"]["toolSearch"].get<bool>()) << "the site's tools are given to the model as they are";
}

TEST(AgentManifest, TheSecretCarriesTheRunAndThePersona) {
    const json sec = M::secret(settings(), run());
    EXPECT_EQ(sec["metadata"]["name"], M::pod_name(kOwner));
    EXPECT_EQ(sec["metadata"]["namespace"], "life-os-agents");
    EXPECT_EQ(sec["metadata"]["labels"][M::kManagedLabel], M::kManagedBy);
    const auto& d = sec["stringData"];
    EXPECT_EQ(d["gateway-token"], "gw-token");
    EXPECT_EQ(d["get-url"], "https://s3/get");
    EXPECT_EQ(d["put-url"], "https://s3/put");
    EXPECT_EQ(json::parse(d["openclaw.json"].get<std::string>())["models"]["providers"]["lifeos"]["apiKey"],
              "proxy-token");
    for (const char* f : {"AGENTS.md", "SOUL.md", "IDENTITY.md", "USER.md"}) {
        EXPECT_FALSE(d[f].get<std::string>().empty()) << f;
    }
}

TEST(AgentManifest, ThePodRestoresRunsAndKeeps) {
    const json pod = M::pod(settings(), run());
    const auto& spec = pod["spec"];
    EXPECT_EQ(pod["metadata"]["name"], M::pod_name(kOwner));
    EXPECT_EQ(spec["restartPolicy"], "Never");
    EXPECT_FALSE(spec["automountServiceAccountToken"].get<bool>());
    EXPECT_TRUE(spec["securityContext"]["runAsNonRoot"].get<bool>());
    EXPECT_EQ(spec["securityContext"]["seccompProfile"]["type"], "RuntimeDefault");

    const auto& restore = container(spec["initContainers"], "restore");
    EXPECT_EQ(restore.value("restartPolicy", ""), "") << "a plain init container";
    EXPECT_NE(restore["command"][2].get<std::string>().find("$GET_URL"), std::string::npos);
    EXPECT_EQ(restore["env"][0]["valueFrom"]["secretKeyRef"]["key"], "get-url");

    const auto& keeper = container(spec["initContainers"], "keeper");
    EXPECT_EQ(keeper["restartPolicy"], "Always") << "a native sidecar: stopped after the gateway";
    const std::string save = keeper["command"][2].get<std::string>();
    EXPECT_NE(save.find("trap save TERM"), std::string::npos);
    EXPECT_NE(save.find("Cache-Control: private, no-store"), std::string::npos) << "signed into the PUT URL";
    EXPECT_NE(save.find("--exclude=./agents/main/agent/codex-home/.tmp"), std::string::npos);
    EXPECT_EQ(keeper["env"][0]["valueFrom"]["secretKeyRef"]["key"], "put-url");

    const auto& gateway = container(spec["containers"], "gateway");
    EXPECT_EQ(gateway["image"], "ghcr.io/openclaw/openclaw:2026.10.7-slim");
    EXPECT_TRUE(gateway["securityContext"]["readOnlyRootFilesystem"].get<bool>());
    EXPECT_EQ(gateway["securityContext"]["capabilities"]["drop"][0], "ALL");
    EXPECT_TRUE(gateway.contains("readinessProbe"));
    for (const auto& e : gateway["env"]) {
        EXPECT_EQ(e["name"].get<std::string>().find("API_KEY"), std::string::npos) << "no provider key in the pod";
    }
    EXPECT_EQ(pod.dump().find("sk-upstream"), std::string::npos);

    const auto& term = spec["affinity"]["nodeAffinity"]["requiredDuringSchedulingIgnoredDuringExecution"];
    EXPECT_EQ(term["nodeSelectorTerms"][0]["matchExpressions"][0]["key"], "node-role.kubernetes.io/worker");
}

TEST(AgentManifest, NoNodeLabelNoAffinity) {
    auto s = settings();
    s.node_label.clear();
    EXPECT_FALSE(M::pod(s, run())["spec"].contains("affinity"));
}

TEST(AgentManifest, SheSpeaksAsAWomanOnFirstNameTerms) {
    // Her Telegram lines came out as «Я предлагаю отправить… Подтвердите, чтобы я отправил».
    const std::string agents = Agent::Manifest::kAgentsMd;
    EXPECT_NE(agents.find("feminine"), std::string::npos);
    EXPECT_NE(agents.find("«ты»"), std::string::npos);
    EXPECT_NE(agents.find("Подтвердите"), std::string::npos) << "named as the form to avoid";
    EXPECT_NE(agents.find("\"YYYY-MM-DD: what\""), std::string::npos) << "dated plans the page follows up";
    EXPECT_NE(agents.find("«Хочу: what (YYYY-MM-DD)»"), std::string::npos) << "wishes the page asks about";
    EXPECT_NE(agents.find("in the person's own voice"), std::string::npos) << "drafts in their style";
    const std::string hook = Agent::hook_note();
    EXPECT_NE(hook.find("feminine"), std::string::npos);
    EXPECT_NE(hook.find("«ты»"), std::string::npos);
}
