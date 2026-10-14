/**
 * @file Digest.hpp
 * @brief The food part of the day's digest: what was eaten against the targets.
 *        The Now screen (GET /api/v1/today, api/TodayController.cpp) reads
 *        each module through one such function and composes nothing itself.
 */

#pragma once

#include <string>

#include <nlohmann/json_fwd.hpp>

namespace Food {

/// {eaten, target} of @p owner on @p day; null when nothing was eaten and no
/// targets are set.
nlohmann::json digest(const std::string& owner, const std::string& day);

}  // namespace Food
