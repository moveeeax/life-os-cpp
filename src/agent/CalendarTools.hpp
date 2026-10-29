/**
 * @file CalendarTools.hpp
 * @brief Ani's tools on the person's Google Calendar (calendar/), given while
 *        it is linked and not broken: the events of a range and the free slots
 *        of a day, read through the site's /api/v1/calendar routes as the user.
 *        Adding, moving and removing an event are proposals (Actions:
 *        calendar_add, calendar_move, calendar_remove). Event ids stay in the
 *        answers: she moves and removes by them.
 */

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "agent/Gateway.hpp"
#include "net/Http.hpp"

namespace Agent::CalendarTools {

/// Whether the person's calendar is linked and working.
bool available(const std::string& owner);

nlohmann::json definitions();

bool is_calendar(const std::string& name);

/// The site request of a call: {path, query}; a refusal text instead.
nlohmann::json request_of(const std::string& name,
                          const nlohmann::json& args,
                          const std::string& tz,
                          std::string& refusal);

Gateway::ToolRunner runner(Net::Http::Transport& transport,
                           const std::string& api_url,
                           const std::string& access_token,
                           const std::string& tz,
                           Gateway::ToolRunner rest);

}  // namespace Agent::CalendarTools
