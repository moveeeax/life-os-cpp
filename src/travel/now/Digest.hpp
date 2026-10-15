/**
 * @file Digest.hpp
 * @brief The travel part of the day's digest: the next trip and the counters near a threshold.
 *        The Now screen (GET /api/v1/today, api/TodayController.cpp) reads
 *        each module through one such function and composes nothing itself.
 */

#pragma once

#include <string>

#include <nlohmann/json_fwd.hpp>

namespace Travel {

/// {next_trip, near} of @p owner on @p day (the shape of Now::Today::travel_block);
/// null when there is no trip and nothing near.
nlohmann::json digest(const std::string& owner, const std::string& day);

}  // namespace Travel
