/**
 * @file Digest.hpp
 * @brief The health part of the day's digest: steps, sleep and weight from the linked Mi account.
 *        The Now screen (GET /api/v1/today, api/TodayController.cpp) reads
 *        each module through one such function and composes nothing itself.
 */

#pragma once

#include <string>

#include <nlohmann/json_fwd.hpp>

namespace Fitness {

/// The health block of @p owner on @p day (the shape of Now::Today::health_block);
/// null when steps, sleep and weight are all missing. Without a weighing from
/// the scale the latest weight the food profile knows stands in.
nlohmann::json digest(const std::string& owner, const std::string& day);

}  // namespace Fitness
