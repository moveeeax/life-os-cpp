/**
 * @file Proxy.hpp
 * @brief The model proxy of the agent pods, served by the worker at
 *        POST /ani/llm/v1/chat/completions. A pod knows no provider key: it
 *        calls this with its run's proxy token, and the proxy
 *          - finds the user by the token's hash (a stopped run's token is dead),
 *          - refuses once the user's month is over its token budget,
 *          - pins the model and asks for usage in a stream,
 *          - forwards to the upstream with the key only the worker holds,
 *          - counts the tokens the answer reports.
 *        The answer is buffered and replayed whole, SSE included: the pod
 *        reads the same events, only later. The lookups are injected, so the
 *        flow is tested without a database.
 */

#pragma once

#include <functional>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "agent/Settings.hpp"
#include "net/Http.hpp"

namespace Agent::Proxy {

inline constexpr const char* kPath = "/ani/llm/v1/chat/completions";
inline constexpr long kUpstreamTimeoutSeconds = 300;

struct Usage {
    long long prompt_tokens = 0;
    long long completion_tokens = 0;
    bool reported = false;
};

struct Reply {
    long status = 200;
    std::string content_type = "application/json";
    std::string body;
};

struct Hooks {
    /// The user whose live run holds the token with this SHA-256.
    std::function<std::optional<std::string>(const std::string& token_hash)> owner_of;
    /// Prompt + completion tokens the user spent this month.
    std::function<long long(const std::string& owner)> month_tokens;
    /// One answered call (its usage may be unreported).
    std::function<void(const std::string& owner, const Usage& usage)> record;
};

/// The usage of an answer: the `usage` object of a JSON body, or of the last SSE
/// event that carries one.
Usage usage_of(const std::string& body, bool stream);

/// The request as the upstream gets it: the configured model, and usage in the
/// last event of a stream.
nlohmann::json rewrite(nlohmann::json request, const Settings& s);

/// The whole exchange for one call.
Reply handle(const std::string& authorization,
             const std::string& body,
             const Settings& s,
             Net::Http::Transport& transport,
             const Hooks& hooks);

}  // namespace Agent::Proxy
