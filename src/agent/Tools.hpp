/**
 * @file Tools.hpp
 * @brief The Life OS tools of Ani's agent: what it may read of the user's
 *        site. A tool call becomes a GET on the site's own API, made by the
 *        worker as the user with an access token minted for the turn (as long
 *        as the longest turn), so the module switches, permissions and validation that
 *        guard the browser guard the agent too, and the pod itself holds no
 *        Life OS credential. What the model reads is the answer's data
 *        without ids and empty values (as the voice's tools, persona.ts).
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent/Gateway.hpp"
#include "net/Http.hpp"

namespace Agent::Tools {

/// How long the access token of one turn lives: the longest turn (kMaxRounds
/// gateway calls of kCallTimeoutSeconds each) plus five minutes for its tools,
/// so a tool call late in a slow turn is not refused with 401.
inline constexpr long kTokenSeconds = Gateway::kMaxRounds * Gateway::kCallTimeoutSeconds + 300;

/// The tools as the gateway takes them (OpenAI function tools).
nlohmann::json definitions();

/// The API request of one tool call: a path and its query.
struct Call {
    std::string path;
    std::vector<std::pair<std::string, std::string>> query;
};

/// The request a call of @p name becomes, in the user's zone @p tz; nullopt
/// for a tool that does not exist. Arguments of the wrong type are dropped
/// (the API's defaults apply).
std::optional<Call> route(const std::string& name, const nlohmann::json& args, const std::string& tz);

/// @p v without ids, nulls, empty lists and error lists.
nlohmann::json compact(const nlohmann::json& v);

/// What the model reads of one API answer: the compacted data, or an error.
std::string result_text(const Net::Http::Response& r);

/// Runs the tools of one turn against the API at @p api_url (no trailing slash).
Gateway::ToolRunner runner(Net::Http::Transport& transport,
                           const std::string& api_url,
                           const std::string& access_token,
                           const std::string& tz);

/// An access token of the user for the tools; nullopt when the user is gone
/// or the process has no JWT secret. It never carries the admin bit: an
/// administrator's token keeps the data permissions the bit stands for, as
/// an API key of theirs does (Security::ApiKeys::kAdminKeyDataBits).
std::optional<std::string> access_token(const std::string& owner);

/// The role permissions a turn's token carries: @p role without the admin
/// bit, plus the data bits an administrator holds through it.
std::uint32_t token_permissions(std::uint32_t role);

/// Whether a turn of @p origin offers the tool @p name. The person's own
/// turns ("user") offer every tool. A webhook turn ("hook": the webhook and
/// Telegram events, other people's text) offers the reads and the proposals
/// the person confirms, nothing that acts at once (remember, forget,
/// promise, drop_promise, tg_react, tg_mark_read, or any tool not known as
/// a read). An MCP turn ("mcp": an API key) offers no Telegram tool.
bool may_offer(const std::string& origin, const std::string& name);

/// @p tools without those a turn of @p origin does not offer.
nlohmann::json offered(const std::string& origin, const nlohmann::json& tools);

/// @p run refusing every tool a turn of @p origin does not offer, so a call
/// the model makes by name anyway changes nothing.
Gateway::ToolRunner offered_runner(const std::string& origin, Gateway::ToolRunner run);

}  // namespace Agent::Tools
