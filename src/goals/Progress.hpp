/**
 * @file Progress.hpp
 * @brief A goal's progress and pace at one date, per kind
 *        (docs/superpowers/specs/2026-10-08-goals-section-design.md §4).
 *        Pure: the caller hands the goal, its check-ins and its task counts.
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "goals/Fields.hpp"
#include "utils/Date.hpp"

namespace Goals::Progress {

/// What the computation needs of a goal.
struct Goal {
    std::string kind;   // number | steps | count | binary
    std::string start;  // YYYY-MM-DD
    std::string due;    // YYYY-MM-DD, after start
    double start_value = 0;
    double target_value = 0;
    int target_count = 0;
    std::string result;  // "" | pass | fail
};

struct Checkin {
    std::string date;
    double value = 0;
};

/// The goal's tasks: how many are done of how many.
struct Counts {
    int done = 0;
    int total = 0;
};

/// A number goal within this share of its whole span behind the even pace is on track.
inline constexpr double kNumberTolerance = 0.02;
/// A steps goal within this many points of the time gone is on track.
inline constexpr double kStepsTolerance = 0.05;
/// A binary goal with fewer days left and no result is behind.
inline constexpr int kBinaryWarnDays = 14;

namespace detail {

inline long days(std::string_view from, std::string_view to) {
    using std::chrono::sys_days;
    return (sys_days{Utils::Date::parse_ymd(to)} - sys_days{Utils::Date::parse_ymd(from)}).count();
}

inline double clamp01(double x) {
    return std::clamp(x, 0.0, 1.0);
}

}  // namespace detail

/**
 * @param checkins The number goal's check-ins, oldest first (others ignore them).
 * @param tasks    The goal's tasks (steps and count use them).
 */
inline nlohmann::json compute(const Goal& g,
                              std::string_view date,
                              const std::vector<Checkin>& checkins,
                              Counts tasks) {
    using detail::clamp01;
    using detail::days;
    const long total = std::max(1L, days(g.start, g.due));
    const double elapsed = clamp01(static_cast<double>(days(g.start, date)) / static_cast<double>(total));
    const long days_left = std::max(0L, days(date, g.due));
    nlohmann::json out{{"kind", g.kind}, {"elapsed", elapsed}, {"days_left", days_left}};

    if (g.kind == "number") {
        double current = g.start_value;
        std::string current_date = g.start;
        for (const auto& c : checkins) {
            if (c.date <= date) {
                current = c.value;
                current_date = c.date;
            }
        }
        const double span = g.target_value - g.start_value;
        const double sign = span < 0 ? -1.0 : 1.0;
        const double expected = g.start_value + span * elapsed;
        const double gap = (current - expected) * sign;
        const double weeks_left = std::max(1.0, static_cast<double>(days_left) / 7.0);
        out["progress"] = clamp01((current - g.start_value) / span);
        out["current"] = current;
        out["current_date"] = current_date;
        out["expected"] = expected;
        out["gap"] = gap;
        out["per_week"] = (g.target_value - current) / weeks_left;
        out["stale"] = days(current_date, date) > Fields::kStaleDays;
        out["pace"] = gap >= -kNumberTolerance * std::abs(span) ? "on_track" : "behind";
        return out;
    }
    if (g.kind == "steps") {
        const double progress = tasks.total > 0 ? static_cast<double>(tasks.done) / tasks.total : 0.0;
        out["progress"] = progress;
        out["done"] = tasks.done;
        out["total"] = tasks.total;
        out["pace"] = progress >= elapsed - kStepsTolerance ? "on_track" : "behind";
        return out;
    }
    if (g.kind == "count") {
        const int target = std::max(1, g.target_count);
        const double expected = target * elapsed;
        out["progress"] = clamp01(static_cast<double>(tasks.done) / target);
        out["done"] = tasks.done;
        out["target"] = target;
        out["expected"] = expected;
        out["reached"] = tasks.done >= target;
        out["pace"] = tasks.done >= static_cast<int>(std::floor(expected + 1e-9)) ? "on_track" : "behind";
        return out;
    }
    // binary: no percent; the time left and the result say it.
    out["progress"] = nullptr;
    out["result"] = g.result.empty() ? nlohmann::json() : nlohmann::json(g.result);
    if (g.result == "pass") {
        out["pace"] = "passed";
    } else if (g.result == "fail") {
        out["pace"] = "failed";
    } else {
        out["pace"] = days_left < kBinaryWarnDays ? "behind" : "on_track";
    }
    return out;
}

}  // namespace Goals::Progress
