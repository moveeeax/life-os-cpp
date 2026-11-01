/**
 * @file CalendarCache.hpp
 * @brief The user's Google Calendar events of a day as GET /api/v1/now/day
 *        shows them, kept in Redis for a minute per user and range: the
 *        ribbon refetches every 5 minutes from every tab that keeps Now, and
 *        each fetch was a token refresh plus an events call. A confirmed add,
 *        move or remove forgets the user's entries at once (a generation
 *        counter, so one INCR drops every range). After Google failed, the
 *        page does not ask again for a minute (a down key) and shows the day
 *        without events, its calendar state "unavailable". Fail-open: with
 *        Redis down nothing is cached and every load asks Google.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "calendar/Google.hpp"

namespace Api::CalendarCache {

/// How long a user's events of a range are kept.
inline constexpr long kTtlSeconds = 60;
/// How long the page leaves Google alone after it failed.
inline constexpr long kDownSeconds = 60;

/// The events of [@p from, @p to) kept for @p owner, if any.
std::optional<std::vector<Calendar::Event>> get(const std::string& owner, long long from, long long to);

void put(const std::string& owner, long long from, long long to, const std::vector<Calendar::Event>& events);

/// Drops every kept range of @p owner and the down key (after a change to the calendar).
void forget(const std::string& owner);

/// Whether Google failed for @p owner in the last kDownSeconds.
bool down(const std::string& owner);
void mark_down(const std::string& owner);

}  // namespace Api::CalendarCache
