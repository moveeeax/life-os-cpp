/**
 * @file Digest.hpp
 * @brief The tasks part of the day's digest: the late and today's tasks and the inbox count.
 *        The Now screen (GET /api/v1/today, api/TodayController.cpp) reads
 *        each module through one such function and composes nothing itself.
 */

#pragma once

#include <string>

#include <nlohmann/json_fwd.hpp>

namespace Tasks {

/// {late_count, first_late, late, today, inbox_count} of @p owner on @p day in @p tz
/// (the shape of Now::Today::tasks_block).
nlohmann::json digest(const std::string& owner, const std::string& day, const std::string& tz);

}  // namespace Tasks
