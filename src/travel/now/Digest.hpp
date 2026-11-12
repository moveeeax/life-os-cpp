/**
 * @file Digest.hpp
 * @brief The travel parts of the day's digest: the next trip and the counters near a threshold,
 *        and the documents running out soon.
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

/// The documents of @p owner valid until at most travel.documents_warn_days
/// (default 180) after @p day, [{id, holder, kind, valid_until, days_left}]
/// the soonest first (Travel::expiring); null when none. The number stays out.
nlohmann::json documents_digest(const std::string& owner, const std::string& day);

}  // namespace Travel
