/**
 * @file Days.hpp
 * @brief The habits evaluated over a range of days: the manual ticks and the
 *        series of the metrics catalog the habits read (repositories/metrics/
 *        Series.hpp, each distinct source once per window), the auto values
 *        computed on read. Shared by GET /api/v1/habits/days and the habits
 *        part of GET /api/v1/today.
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "habits/Habits.hpp"
#include "metrics/Catalog.hpp"
#include "repositories/habits/HabitRepository.hpp"
#include "repositories/metrics/Series.hpp"

namespace Repositories::Habits {

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

/// The longest window windows() returns: the history year plus a 62-day range.
inline constexpr int kWindowsMaxDays = kHistoryDays + 1 + 62;

/**
 * The day windows the sources are read for: the range [@p from, @p to], and the
 * days the streak needs (the last kHistoryDays up to @p today with @p history,
 * else @p today alone). Overlapping or adjacent windows become one. A range far
 * from today is read on its own instead of as one window stretching to today,
 * which would read years of rows nobody shows.
 */
inline std::vector<std::pair<::Habits::Day, ::Habits::Day>> windows(::Habits::Day from,
                                                                    ::Habits::Day to,
                                                                    ::Habits::Day today,
                                                                    bool history) {
    using std::chrono::days;
    std::pair<::Habits::Day, ::Habits::Day> range{from, to};
    std::pair<::Habits::Day, ::Habits::Day> streak{history ? today - days{kHistoryDays} : today, today};
    if (streak.first < range.first) {
        std::swap(range, streak);
    }
    // range now starts first.
    if (streak.first <= range.second + days{1}) {
        return {{range.first, std::max(range.second, streak.second)}};
    }
    return {range, streak};
}

inline ::Habits::Habit model_of(const nlohmann::json& row) {
    ::Habits::Habit h;
    h.schedule = ::Habits::schedule_of(row.at("schedule").get<std::string>()).value_or(::Habits::Schedule::Daily);
    h.per_week = row.at("per_week").is_number() ? row.at("per_week").get<int>() : 0;
    h.goal = row.at("goal").is_number() ? std::optional<double>(row.at("goal").get<double>()) : std::nullopt;
    h.source = ::Habits::source_of(row.at("source").get<std::string>());
    h.direction =
        row.contains("direction") && row.at("direction").is_string()
            ? ::Habits::direction_of(row.at("direction").get<std::string>()).value_or(::Habits::Direction::AtLeast)
            : ::Habits::Direction::AtLeast;
    h.target = row.contains("target") && row.at("target").is_number()
                   ? std::optional<double>(row.at("target").get<double>())
                   : std::nullopt;
    return h;
}

/**
 * The owner's active habits over [@p from, @p to] on @p today (the caller's local
 * day in @p tz). With @p history the sources are read for the last 365 days too, so
 * the streak and the best are whole; without it only the range is read (the plate).
 * A source the caller cannot read (@p access: the module off, no fitness read
 * permission, no Mi account, a habit or a category that is not theirs) gives no
 * values and no_data; the ticks by hand still count.
 */
inline std::vector<Evaluated> evaluate(const std::string& owner,
                                       ::Habits::Day from,
                                       ::Habits::Day to,
                                       ::Habits::Day today,
                                       const std::string& tz,
                                       const Repositories::Metrics::Access& access,
                                       bool history) {
    using std::chrono::days;
    HabitRepository repo;
    const auto rows = repo.list(owner, false);
    std::vector<Evaluated> out;
    if (rows.empty()) {
        return out;
    }
    std::vector<::Habits::Habit> models;
    models.reserve(rows.size());
    for (const auto& row : rows) {
        models.push_back(model_of(row));
    }

    // Each distinct source once per window, its values kept to the window's own days.
    const auto read_windows = windows(from, to, today, history);
    std::map<std::string, Repositories::Metrics::Series> series;
    for (const auto& id : ::Habits::distinct_sources(models)) {
        const auto ref = ::Metrics::parse(id);
        Repositories::Metrics::Series s;
        for (const auto& [lo, hi] : read_windows) {
            const auto window = Repositories::Metrics::read(owner, *ref, lo, hi, tz, access);
            s.why = window.why;
            if (window.why != Repositories::Metrics::Why::Ok) {
                s.values.clear();
                break;
            }
            for (const auto& [day, value] : window.values) {
                if (day >= lo && day <= hi) {
                    s.values.emplace(day, value);
                }
            }
        }
        series.emplace(id, std::move(s));
    }

    // The ticks are read for the same span as the sources, not for all time: /today asks every
    // load for this week only, and the streak and the best reach back kHistoryDays at most.
    ::Habits::Day first = read_windows.front().first;
    ::Habits::Day last = read_windows.front().second;
    for (const auto& [lo, hi] : read_windows) {
        first = std::min(first, lo);
        last = std::max(last, hi);
    }
    const auto checks = repo.checks(owner, ::Habits::text_of(first), ::Habits::text_of(last));
    static const ::Habits::Checks kNoChecks;
    static const ::Habits::Values kNoValues;

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        Evaluated e;
        e.habit = row;
        e.model = models[i];
        const ::Habits::Values* values = &kNoValues;
        if (e.model.source && e.model.source->def != nullptr) {
            const auto& s = series.at(::Metrics::canonical(*e.model.source));
            values = &s.values;
            e.no_data = s.why != Repositories::Metrics::Why::Ok;
        }
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
