/**
 * @file Days.hpp
 * @brief The habits evaluated over a range of days: the manual ticks, the Mi
 *        Fitness values (sleep, steps, active kcal) and the Training sessions
 *        read for the range, the auto values computed on read. Shared by
 *        GET /api/v1/habits/days and the habits part of GET /api/v1/today.
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "habits/Habits.hpp"
#include "repositories/fitness/HealthReadRepository.hpp"
#include "repositories/fitness/MiAccountRepository.hpp"
#include "repositories/habits/HabitRepository.hpp"

namespace Repositories::Habits {

/// What the caller may read: the Mi Fitness tables (the fitness module on and the
/// fitness read permission) and the Training sessions (the workout module on).
struct Access {
    bool health = false;
    bool workout = false;
};

/// How far back the auto values reach for the streaks.
inline constexpr int kHistoryDays = 365;

struct Evaluated {
    nlohmann::json habit;  // the stored row
    ::Habits::Habit model;
    std::vector<std::pair<::Habits::Day, ::Habits::DayState>> days;
    int streak = 0;
    int best = 0;
    int week_done = 0;  // done days in the week of the range's last day
    bool no_data = false;
};

inline ::Habits::Habit model_of(const nlohmann::json& row) {
    ::Habits::Habit h;
    h.schedule = ::Habits::schedule_of(row.at("schedule").get<std::string>()).value_or(::Habits::Schedule::Daily);
    h.per_week = row.at("per_week").is_number() ? row.at("per_week").get<int>() : 0;
    h.goal = row.at("goal").is_number() ? std::optional<double>(row.at("goal").get<double>()) : std::nullopt;
    h.source = ::Habits::source_of(row.at("source").get<std::string>()).value_or(::Habits::Source::Manual);
    return h;
}

/**
 * The owner's active habits over [@p from, @p to] on @p today (the caller's local
 * day in @p tz). With @p history the sources are read for the last 365 days too, so
 * the streak and the best are whole; without it only the range is read (the plate).
 */
inline std::vector<Evaluated> evaluate(const std::string& owner,
                                       ::Habits::Day from,
                                       ::Habits::Day to,
                                       ::Habits::Day today,
                                       const std::string& tz,
                                       const Access& access,
                                       bool history) {
    using ::Habits::Source;
    using std::chrono::days;
    HabitRepository repo;
    const auto rows = repo.list(owner, false);
    std::vector<Evaluated> out;
    if (rows.empty()) {
        return out;
    }
    bool wants_health = false;
    bool wants_workout = false;
    for (const auto& row : rows) {
        const auto m = model_of(row);
        wants_health = wants_health || ::Habits::from_health(m.source);
        wants_workout = wants_workout || m.source == Source::Workout;
    }
    const ::Habits::Day lo = history ? std::min(from, today - days{kHistoryDays}) : from;
    const ::Habits::Day hi = std::max(to, today);

    ::Habits::Values sleep;
    ::Habits::Values steps;
    ::Habits::Values kcal;
    ::Habits::Values workouts;
    bool health_linked = false;
    if (wants_health && access.health) {
        const std::string xiaomi = MiAccountRepository("").xiaomi_id_of(owner).value_or("");
        health_linked = !xiaomi.empty();
        if (health_linked) {
            HealthReadRepository health(xiaomi);
            constexpr long kRows = 2000;  // a year of nights and days with room for naps
            // Sleep rows are filed by the UTC day of their end; the region day may start hours earlier.
            sleep = ::Habits::sleep_by_local_day(
                health.sleep(::Habits::text_of(lo - days{1}), ::Habits::text_of(hi + days{1}), kRows, 0).rows,
                region_offset_seconds(owner));
            const auto activity = health.daily_activity(::Habits::text_of(lo), ::Habits::text_of(hi), kRows, 0).rows;
            steps = ::Habits::activity_by_day(activity, "steps");
            kcal = ::Habits::activity_by_day(activity, "active_kcal");
        }
    }
    if (wants_workout && access.workout) {
        workouts =
            ::Habits::workouts_by_local_day(repo.sessions(owner, ::Habits::text_of(lo), ::Habits::text_of(hi), tz));
    }
    const auto checks = repo.checks(owner);
    static const ::Habits::Checks kNoChecks;
    static const ::Habits::Values kNoValues;

    for (const auto& row : rows) {
        Evaluated e;
        e.habit = row;
        e.model = model_of(row);
        const ::Habits::Values* values = &kNoValues;
        switch (e.model.source) {
            case Source::Sleep:
                values = &sleep;
                break;
            case Source::Steps:
                values = &steps;
                break;
            case Source::ActiveKcal:
                values = &kcal;
                break;
            case Source::Workout:
                values = &workouts;
                break;
            case Source::Manual:
                break;
        }
        e.no_data = (::Habits::from_health(e.model.source) && !health_linked) ||
                    (e.model.source == Source::Workout && !access.workout);
        const auto c = checks.find(row.at("id").get<std::string>());
        const ::Habits::Checks& manual = c == checks.end() ? kNoChecks : c->second;
        for (auto d = from; d <= to; d += days{1}) {
            e.days.emplace_back(d, ::Habits::day_state(e.model, d, manual, *values));
        }
        // Days after today carry no record yet; a tick there is refused anyway.
        auto done = ::Habits::done_days(e.model, manual, *values);
        done.erase(done.upper_bound(today), done.end());
        e.streak = ::Habits::streak(e.model, done, today);
        e.best = std::max(::Habits::best(e.model, done), e.streak);
        e.week_done = ::Habits::week_count(done, to);
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace Repositories::Habits
