/**
 * @file Access.hpp
 * @brief An access token of a user's Google Calendar: kept in memory until a
 *        minute before it expires, refreshed from the sealed refresh token.
 *        A refusal from Google (invalid_grant) marks the link broken once and
 *        is not retried until the user connects again.
 */

#pragma once

#include <string>
#include <variant>
#include <vector>

#include "calendar/Google.hpp"
#include "net/Http.hpp"

namespace Calendar {

/// Why there is no token: 404 calendar_not_configured / calendar_not_linked,
/// 409 calendar_disconnected (Google refused, connect again), 502 calendar_unavailable.
struct Problem {
    int status = 502;
    std::string code;
};

std::variant<std::string, Problem> access(Net::Http::Transport& t, const std::string& owner);

/// The user's primary-calendar events overlapping [@p from, @p to) (unix
/// seconds); all-day dates read in @p tz. Single events (recurrences expanded).
std::variant<std::vector<Event>, Problem> events(
    Net::Http::Transport& t, const std::string& owner, long long from, long long to, const std::string& tz);

/// RFC 3339 in UTC for a unix time.
std::string rfc3339(long long t);

/// Forgets the cached token of @p owner (after connect or disconnect).
void forget(const std::string& owner);

}  // namespace Calendar
