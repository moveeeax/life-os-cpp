/**
 * @file Actions.hpp
 * @brief The write tools of Ani's agent, which never write by themselves. A
 *        call is checked and resolved against the site's API (a task found
 *        by its words, a habit by its name) and becomes a proposal: an
 *        ani_actions row (migration 061) with the words the user confirms
 *        and the API request that does it. The chat shows it under her
 *        reply with Confirm and Cancel; a confirmed action is run by the
 *        worker in the job ani_action, as the user, like the read tools
 *        (agent/Tools.hpp). Payload: {"action": "<action id>"}.
 */

#pragma once

#include <string>
#include <variant>

#include <nlohmann/json.hpp>

#include "agent/Gateway.hpp"
#include "net/Http.hpp"

namespace Agent::Actions {

inline constexpr const char* kActionJob = "ani_action";
/// Proposals per turn.
inline constexpr int kMaxPerTurn = 3;

/// The write tools as the gateway takes them.
nlohmann::json definitions();

/// The Telegram write tools (tg_send, tg_edit), given only to the user
/// Telegram serves (agent/TelegramTools.hpp).
nlohmann::json telegram_definitions();

/// The calendar write tools (calendar_add, calendar_move, calendar_remove),
/// given while the person's calendar is linked (agent/CalendarTools.hpp).
nlohmann::json calendar_definitions();

/// Whether @p name is one of the write tools.
bool is_write(const std::string& name);

/// What a write call becomes: the kind, the words the user confirms and the
/// request, {method, path, query: [[k, v]...], body}.
struct Proposal {
    std::string kind;
    std::string summary;
    nlohmann::json request;
};

/// A proposal, or what the model reads instead (an error, the candidates of
/// an ambiguous name). @p today is the user's day (YYYY-MM-DD) in @p tz.
using Proposed = std::variant<Proposal, std::string>;

/// Resolves a write call against the API at @p api_url as the user.
Proposed propose(Net::Http::Transport& transport,
                 const std::string& api_url,
                 const std::string& access_token,
                 const std::string& tz,
                 const std::string& today,
                 const std::string& name,
                 const nlohmann::json& args);

/// What the model reads of a proposal it made.
std::string proposed_text(const Proposal& p);

/// Runs @p request against the API; the error code of a refusal, or empty when it went through.
std::string execute(Net::Http::Transport& transport,
                    const std::string& api_url,
                    const std::string& access_token,
                    const nlohmann::json& request);

/// The tool runner of a turn: write calls become proposals of @p turn_id
/// (at most kMaxPerTurn), the rest goes to @p reads.
Gateway::ToolRunner runner(Net::Http::Transport& transport,
                           const std::string& api_url,
                           const std::string& access_token,
                           const std::string& tz,
                           const std::string& today,
                           const std::string& owner,
                           const std::string& turn_id,
                           Gateway::ToolRunner reads);

/// The job handler (registered in agent/jobs/Register.cpp).
nlohmann::json process_action(const nlohmann::json& payload);

/// Fails an action whose job died (the dead-letter hook).
void fail_action(const std::string& action_id, const std::string& error);

}  // namespace Agent::Actions
