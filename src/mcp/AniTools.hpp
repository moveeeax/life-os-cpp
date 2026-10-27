/**
 * @file AniTools.hpp
 * @brief Ani's MCP tools for one user (docs/ANI-MCP.md): the webhook's four
 *        commands (say, outfit, act, event) under its rules, whether the Now
 *        page is open, what became of the items sent to her, and the chat
 *        with her agent. A refusal is a tool error the model can read, never
 *        a protocol error.
 */

#pragma once

#include <functional>
#include <string>

#include "mcp/Protocol.hpp"

namespace Mcp {

/// The longest wait of ani_reply, in one-second checks.
inline constexpr int kReplyWaitMax = 25;
inline constexpr int kItemsDefault = 10;
inline constexpr int kHistoryDefault = 20;
inline constexpr int kListMax = 50;

/// Runs @p fn after @p seconds (the Drogon loop's timer in production).
using Scheduler = std::function<void(double seconds, std::function<void()> fn)>;

/// The loop's timer: drogon::app().getLoop()->runAfter.
Scheduler loop_scheduler();

Server ani_server(const std::string& owner, Scheduler after = loop_scheduler());

}  // namespace Mcp
