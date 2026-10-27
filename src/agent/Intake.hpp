/**
 * @file Intake.hpp
 * @brief Takes one parsed item for Ani's inbox from her webhook or her MCP,
 *        with the same rules for both: bare only while it is on, events only
 *        while her agent can take them, at most Hook::kPendingMax waiting, a
 *        sender's id once in 24 hours. The hourly call budget is the
 *        caller's to count (within_rate), before parsing. A hold is applied
 *        at once (and kept as a delivered item); a reminder waits until due;
 *        an ask's callback must not point to a private address.
 */

#pragma once

#include <string>
#include <variant>

#include <nlohmann/json.hpp>

#include "agent/Hook.hpp"
#include "repositories/agent/InboxRepository.hpp"

namespace Agent::Intake {

struct Accepted {
    std::string id;
};

/// An HTTP status and an error code, as the webhook answers them.
struct Refused {
    int status = 400;
    std::string code;
    std::string message;
    nlohmann::json extra = nlohmann::json::object();
};

/// Counts a call of the user's webhook or MCP command; true while within
/// Hook::kCallsPerHour this hour (both share the one counter).
bool within_rate(const std::string& owner);

/// Stores @p item for the user. @p via ("webhook" or "mcp") goes into the
/// item's payload; only the webhook marks the hook as used.
std::variant<Accepted, Refused> accept(const std::string& owner, Hook::Item item, const std::string& via);

/// The user's answer to an ask (from the Now page); on Ok its callback, if
/// any, is queued at once.
Repositories::Agent::InboxRepository::Answer answer(const std::string& owner,
                                                    const std::string& id,
                                                    const std::string& text);

/// Queues the callbacks of answered and unanswered-expired asks, each once
/// (outbound webhook jobs, signed with the user's webhook token); returns
/// how many. Nothing happens without the job queue.
int send_callbacks();

}  // namespace Agent::Intake
