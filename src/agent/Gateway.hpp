/**
 * @file Gateway.hpp
 * @brief One chat turn with a user's agent pod over its OpenClaw gateway
 *        (POST /v1/chat/completions, the `user` field picks the session).
 *        The site's tools are offered as client tools: when the agent calls
 *        them, the worker runs them and sends the whole exchange back (the
 *        user message, the assistant's tool calls, a `role: tool` result per
 *        call) until the agent answers in words. The gateway ignores system
 *        messages, so the caller puts any context into the user message.
 *        The transport is injected, so the loop is tested with a scripted
 *        gateway.
 */

#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "net/Http.hpp"

namespace Agent::Gateway {

inline constexpr int kGatewayPort = 18789;
/// Rounds of tool calls before a turn gives up.
inline constexpr int kMaxRounds = 8;
/// A tool result longer than this is cut: the model reads all of it as input.
inline constexpr std::size_t kToolResultMax = 6000;
/// One gateway call: the agent's own tools and the model run inside it.
inline constexpr long kCallTimeoutSeconds = 120;

/// The turn did not end in a reply. @p code is what the turn row records:
/// "unavailable" (no answer, or the gateway refused) or "failed" (no words
/// after kMaxRounds, or an answer that is not a chat completion).
/// @p not_ready: the gateway refused the first call because its agent is
/// still being prepared (it listens, and passes /readyz, before that ends);
/// nothing reached the model, so the turn may simply be sent again.
struct Error : std::runtime_error {
    Error(std::string c, const std::string& what, bool nr = false)
        : std::runtime_error(what), code(std::move(c)), not_ready(nr) {}
    std::string code;
    bool not_ready = false;
};

/// What OpenClaw answers while its agent has not finished starting up.
inline constexpr const char* kNotReadyMarker = "has not completed startup";

/// Runs one tool: its name and parsed arguments in, the text the model reads out.
using ToolRunner = std::function<std::string(const std::string& name, const nlohmann::json& args)>;

struct Outcome {
    std::string reply;
    std::vector<std::string> tools;  // the tools run, in order
};

/// @p base_url is the pod's gateway ("http://<pod ip>:18789"), @p session the
/// `user` its session is keyed by, @p tools the OpenAI-style tool list.
Outcome converse(Net::Http::Transport& transport,
                 const std::string& base_url,
                 const std::string& token,
                 const std::string& session,
                 const std::string& text,
                 const nlohmann::json& tools,
                 const ToolRunner& run);

}  // namespace Agent::Gateway
