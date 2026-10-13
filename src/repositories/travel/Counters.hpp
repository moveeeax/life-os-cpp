/**
 * @file Counters.hpp
 * @brief The owner's day counters on a date, as JSON: every rule with how many
 *        days are used and left, when a running stay meets the threshold, and
 *        when a full stay fits again. Shared by /api/v1/travel/counters and the
 *        travel block of /api/v1/today.
 */

#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "repositories/travel/CrossingRepository.hpp"
#include "repositories/travel/RuleRepository.hpp"
#include "repositories/travel/SettingsRepository.hpp"
#include "travel/Days.hpp"

namespace Repositories::Travel {

/// Where the days before the first crossing are spent: the setting, else the
/// country the first crossing leaves, else nowhere.
inline std::string home_of(const std::string& owner, const std::vector<::Travel::Crossing>& crossings) {
    if (auto home = SettingsRepository().home(owner)) {
        return *home;
    }
    return crossings.empty() ? std::string() : crossings.front().from;
}

inline nlohmann::json counters_json(const std::string& owner, const std::string& date) {
    const auto crossings = CrossingRepository().all(owner);
    const std::string home = home_of(owner, crossings);
    const auto today = ::Travel::day_of(date);
    nlohmann::json out = nlohmann::json::array();
    for (const auto& stored : RuleRepository().all(owner)) {
        const auto c = ::Travel::count(stored.rule, crossings, home, today);
        const auto& row = stored.row;
        out.push_back({
            {"id", row["id"]},
            {"name", row["name"]},
            {"countries", row["countries"]},
            {"window_kind", row["window_kind"]},
            {"window_days", row["window_days"]},
            {"threshold_days", row["threshold_days"]},
            {"threshold_kind", row["threshold_kind"]},
            {"used", c.used},
            {"left", c.left},
            {"reached_on", c.reached_on ? nlohmann::json(::Travel::text_of(*c.reached_on)) : nlohmann::json()},
            {"reentry", c.reentry ? nlohmann::json(::Travel::text_of(*c.reentry)) : nlohmann::json()},
            {"source", row["source"]},
        });
    }
    return out;
}

}  // namespace Repositories::Travel
