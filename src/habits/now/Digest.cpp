/**
 * @file Digest.cpp
 * @brief Body for src/habits/now/Digest.hpp — compiled once into app_core.
 */

#include "habits/now/Digest.hpp"

#include <vector>

#include <nlohmann/json.hpp>

#include "habits/Habits.hpp"
#include "repositories/habits/Days.hpp"

namespace Habits {

nlohmann::json digest(
    const std::string& owner, const std::string& day, const std::string& tz, bool health_read, bool workout_read) {
    const auto today = day_of(day);
    const Repositories::Habits::Access access{health_read, workout_read};
    const auto evaluated =
        Repositories::Habits::evaluate(owner, monday_of(today), today, today, tz, access, /*history=*/false);
    if (evaluated.empty()) {
        return nlohmann::json();
    }
    std::vector<PlateItem> items;
    for (const auto& e : evaluated) {
        items.push_back({e.habit.at("name").get<std::string>(),
                         e.model.schedule,
                         e.model.per_week,
                         !e.days.empty() && e.days.back().second.done,
                         e.week_done});
    }
    return plate(items);
}

}  // namespace Habits
