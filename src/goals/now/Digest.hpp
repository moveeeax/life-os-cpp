/**
 * @file Digest.hpp
 * @brief The goals part of the day's digest: the active goals with their progress on the day.
 *        The Now screen (GET /api/v1/today, api/TodayController.cpp) reads
 *        each module through one such function and composes nothing itself.
 */

#pragma once

#include <string>

#include <nlohmann/json_fwd.hpp>

namespace Goals {

/// The active goals of @p owner with their progress on @p day, behind first
/// (the shape of Now::Today::goals_block); passed and failed goals are left out.
nlohmann::json digest(const std::string& owner, const std::string& day);

}  // namespace Goals
