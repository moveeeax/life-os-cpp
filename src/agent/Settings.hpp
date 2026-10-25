/**
 * @file Settings.hpp
 * @brief Ani's agent: an OpenClaw pod per user, started when the user comes to
 *        Ani and stopped when they leave (docs/superpowers specs, 2026-10-25).
 *        The worker owns the pods (it alone holds Kubernetes rights) and the
 *        model proxy the pods call; the API only flips the state and queues
 *        the jobs. These are the knobs both read.
 */

#pragma once

#include <optional>
#include <string>

#include "utils/Config.hpp"

namespace Agent {

struct Settings {
    std::string image;                      // the OpenClaw image, pinned
    std::string keeper_image;               // curl + tar: restores and saves the state
    std::string ns;                         // the namespace of the pods
    std::string node_label;                 // a node label key the pods require (the worker nodes); empty: any node
    int max_running = 8;                    // pods at once, across users
    int idle_minutes = 20;                  // stopped after this long without a wake or a model call
    int max_hours = 12;                     // stopped after this long whatever it does
    std::string model;                      // the upstream model every call is pinned to
    std::string upstream_url;               // an OpenAI-compatible base URL, e.g. https://api.openai.com/v1
    std::string upstream_key;               // its key: never leaves the worker
    std::string proxy_url;                  // the worker's model proxy as the pods reach it, ending in /v1
    long long monthly_tokens = 20'000'000;  // prompt + completion tokens per user and month
};

/// Whether the agent is switched on (ani.agent.enabled).
inline bool enabled() {
    return Config::is_initialized() && Config::get().get<bool>("ani.agent.enabled", "ANI_AGENT_ENABLED", false);
}

/// The settings; nullopt while the agent is off or a required knob is missing.
inline std::optional<Settings> read_settings() {
    if (!enabled()) {
        return std::nullopt;
    }
    auto& cfg = Config::get();
    Settings s;
    s.image = cfg.get<std::string>("ani.agent.image", "ANI_AGENT_IMAGE", "ghcr.io/openclaw/openclaw:2026.10.7-slim");
    s.keeper_image = cfg.get<std::string>("ani.agent.keeper_image", "ANI_AGENT_KEEPER_IMAGE", "curlimages/curl:8.17.0");
    s.ns = cfg.get<std::string>("ani.agent.namespace", "ANI_AGENT_NAMESPACE", "life-os-agents");
    s.node_label =
        cfg.get<std::string>("ani.agent.node_label", "ANI_AGENT_NODE_LABEL", "node-role.kubernetes.io/worker");
    s.max_running = cfg.get<int>("ani.agent.max_running", "ANI_AGENT_MAX_RUNNING", 8);
    s.idle_minutes = cfg.get<int>("ani.agent.idle_minutes", "ANI_AGENT_IDLE_MINUTES", 20);
    s.max_hours = cfg.get<int>("ani.agent.max_hours", "ANI_AGENT_MAX_HOURS", 12);
    s.model = cfg.get<std::string>("ani.agent.model", "ANI_AGENT_MODEL", "gpt-5-mini");
    s.upstream_url =
        cfg.get<std::string>("ani.agent.upstream_url", "ANI_AGENT_UPSTREAM_URL", "https://api.openai.com/v1");
    s.upstream_key = cfg.get<std::string>("ani.agent.upstream_key", "ANI_AGENT_UPSTREAM_KEY", "");
    s.proxy_url = cfg.get<std::string>("ani.agent.proxy_url", "ANI_AGENT_PROXY_URL", "");
    s.monthly_tokens = cfg.get<long>("ani.agent.monthly_tokens", "ANI_AGENT_MONTHLY_TOKENS", 20000000L);
    if (s.image.empty() || s.keeper_image.empty() || s.ns.empty() || s.model.empty() || s.upstream_url.empty() ||
        s.upstream_key.empty() || s.proxy_url.empty() || s.max_running < 1 || s.idle_minutes < 1 || s.max_hours < 1) {
        return std::nullopt;
    }
    return s;
}

}  // namespace Agent
