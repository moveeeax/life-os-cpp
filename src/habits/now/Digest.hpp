/**
 * @file Digest.hpp
 * @brief The habits part of the day's digest: the plate of the week read up to the day.
 *        The Now screen (GET /api/v1/today, api/TodayController.cpp) reads
 *        each module through one such function and composes nothing itself.
 */

#pragma once

#include <string>

#include <nlohmann/json_fwd.hpp>

namespace Habits {

/// The Now plate of @p owner's habits: the week of @p day read up to @p day in
/// @p tz; null without habits. @p health_read and @p workout_read say which
/// sources the caller may read (the fitness and workout switches and the
/// fitness read permission), as Repositories::Habits::Access.
nlohmann::json digest(
    const std::string& owner, const std::string& day, const std::string& tz, bool health_read, bool workout_read);

}  // namespace Habits
