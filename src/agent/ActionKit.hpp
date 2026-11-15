/**
 * @file ActionKit.hpp
 * @brief Inside the write tools of agent/Actions.hpp: what a proposal is
 *        resolved with, the argument helpers they share, and each write
 *        tool's definition and proposal by domain (TaskActions.cpp,
 *        LedgerActions.cpp, TelegramActions.cpp, CalendarActions.cpp). The
 *        table in Actions.cpp lists them; nothing outside agent/ includes this.
 */

#pragma once

#include <cstddef>
#include <string>

#include <nlohmann/json.hpp>

#include "agent/Actions.hpp"
#include "agent/ToolArgs.hpp"
#include "net/Http.hpp"
#include "utils/Utf8.hpp"

namespace Agent::Actions::detail {

using json = nlohmann::json;

/// What a write call is resolved with: the site's API as the user, the
/// person's zone and their day (YYYY-MM-DD).
struct Ctx {
    Net::Http::Transport& transport;
    const std::string& api_url;
    const std::string& token;
    const std::string& tz;
    const std::string& today;
};

using ToolArgs::object;
using ToolArgs::refusal;
using ToolArgs::tool;
using Utils::Utf8::length;

/// A string argument without the spaces around it; empty when missing or not a string.
inline std::string text_arg(const json& args, const char* key) {
    return ToolArgs::text_arg(args, key, /*trim=*/true);
}

/// A day argument; anything that is not a real YYYY-MM-DD day is dropped.
inline std::string day_arg(const json& args, const char* key) {
    return ToolArgs::day_arg(args, key, /*trim=*/true);
}

/// The first @p max characters of @p s, with an ellipsis when it was longer.
inline std::string cut(const std::string& s, std::size_t max) {
    return Utils::Utf8::clip(s, max);
}

// Tasks, habits, notes and the diary (TaskActions.cpp).
json create_task_tool();
Proposed create_task(const Ctx& c, const json& args);
json complete_task_tool();
Proposed complete_task(const Ctx& c, const json& args);
json check_habit_tool();
Proposed check_habit(const Ctx& c, const json& args);
json add_note_tool();
Proposed add_note(const Ctx& c, const json& args);
json journal_note_tool();
Proposed journal_note(const Ctx& c, const json& args);

// The food diary and the money ledger (LedgerActions.cpp).
json add_food_tool();
Proposed add_food(const Ctx& c, const json& args);
json add_spend_tool();
Proposed add_spend(const Ctx& c, const json& args);

// Telegram (TelegramActions.cpp).
json tg_send_tool();
Proposed tg_send(const Ctx& c, const json& args);
json tg_edit_tool();
Proposed tg_edit(const Ctx& c, const json& args);

// Google Calendar (CalendarActions.cpp).
json calendar_add_tool();
Proposed calendar_add(const Ctx& c, const json& args);
json calendar_move_tool();
Proposed calendar_move(const Ctx& c, const json& args);
json calendar_remove_tool();
Proposed calendar_remove(const Ctx& c, const json& args);

}  // namespace Agent::Actions::detail
