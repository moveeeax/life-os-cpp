/**
 * @file TelegramTools.hpp
 * @brief Ani's tools on the person's Telegram, given only to the user the
 *        owner's gateway account serves (telegram/). Reading (tg_chats,
 *        tg_history, tg_search) and the small things she does by herself
 *        (tg_react, tg_mark_read, owner's rule 2026-10-28) are calls to the
 *        site's /api/v1/telegram routes as the user, like her other tools;
 *        sending and editing are proposals (Actions: tg_send, tg_edit). The
 *        answers keep their ids: she needs them to react or reply.
 */

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "agent/Gateway.hpp"
#include "net/Http.hpp"

namespace Agent::TelegramTools {

/// Whether Telegram serves @p owner (the gateway is set up and linked to them).
bool available(const std::string& owner);

/// The read and do-it-yourself tools as the gateway takes them.
nlohmann::json definitions();

/// Whether @p name is one of them.
bool is_telegram(const std::string& name);

/// The site request a call of @p name becomes: {method, path, query: [[k, v]...], body}; a refusal text instead.
nlohmann::json request_of(const std::string& name, const nlohmann::json& args, std::string& refusal);

/// Runs the Telegram tools against the API at @p api_url as the user; the rest goes to @p rest.
Gateway::ToolRunner runner(Net::Http::Transport& transport,
                           const std::string& api_url,
                           const std::string& access_token,
                           Gateway::ToolRunner rest);

}  // namespace Agent::TelegramTools
