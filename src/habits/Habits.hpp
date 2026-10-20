/**
 * @file Habits.hpp
 * @brief The habit evaluation (docs/superpowers/specs/2026-10-14-habits-design.md,
 *        sources from docs/superpowers/specs/2026-10-20-metric-sources-design.md).
 *        Pure: no database, no clock; "today" is always passed in.
 *
 *        A day is done when it has a manual check (with a daily target, one
 *        whose value, the sum of the day, reaches the target) or the habit's
 *        source, a series of the metrics catalog (metrics/Catalog.hpp), meets
 *        it that day: the value at least or at most the goal, or, for a
 *        presence entry without a goal (a weigh-in, a session, a mood), any
 *        value at all. A daily habit streaks over consecutive done days ending
 *        yesterday, plus today when today is done; a weekly habit (Monday to Sunday) over
 *        consecutive weeks with at least per_week done days, ending last week,
 *        plus this week when met. An open today or week never breaks a streak.
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "metrics/Catalog.hpp"
#include "utils/Date.hpp"

namespace Habits {

using Day = std::chrono::sys_days;
using json = nlohmann::json;

inline Day day_of(std::string_view text) {
    return Day{Utils::Date::parse_ymd(text)};
}

inline std::string text_of(Day d) {
    const std::chrono::year_month_day ymd{d};
    char buf[32];  // room for any int year: GCC checks the worst case
    std::snprintf(buf,
                  sizeof buf,
                  "%04d-%02u-%02u",
                  static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()),
                  static_cast<unsigned>(ymd.day()));
    return buf;
}

enum class Schedule { Daily, Weekly };
/// How a source's value of the day compares to the goal.
enum class Direction { AtLeast, AtMost };

inline std::optional<Schedule> schedule_of(std::string_view s) {
    if (s == "daily") {
        return Schedule::Daily;
    }
    if (s == "weekly") {
        return Schedule::Weekly;
    }
    return std::nullopt;
}

inline std::optional<Direction> direction_of(std::string_view s) {
    if (s == "at_least") {
        return Direction::AtLeast;
    }
    if (s == "at_most") {
        return Direction::AtMost;
    }
    return std::nullopt;
}

/// The catalog series a stored or sent source names: nullopt for "manual" and for a
/// value that names nothing. Metrics::parse maps the five sources of 1.33 (sleep,
/// steps, active_kcal, workout, weight) to their catalog ids.
inline std::optional<Metrics::Ref> source_of(std::string_view s) {
    if (s == "manual") {
        return std::nullopt;
    }
    return Metrics::parse(s);
}

struct Habit {
    Schedule schedule = Schedule::Daily;
    int per_week = 0;  // weekly only, 1..7
    std::optional<double> goal;
    /// The catalog series that ticks the day besides a manual tick; nullopt for a habit ticked by hand.
    std::optional<Metrics::Ref> source;
    Direction direction = Direction::AtLeast;
    /// A hand-ticked habit's daily target: a check counts only when its value reaches it.
    std::optional<double> target;
};

/// The value of a day that has a record but no number in it: a weigh-in whose
/// weight_kg is null, a tick by hand without a value. Without a goal a presence
/// source counts the day (meets() looks at the record, not the number); against a
/// goal it does not. day_state() reports its value as null.
inline constexpr double kNoNumber = std::numeric_limits<double>::quiet_NaN();

/**
 * Whether a day's source value meets the habit. With a goal the value compares to
 * it by the direction, in the catalog's unit (sleep in hours). Without a goal a
 * presence entry of the catalog is met by being there at all; any other entry is
 * never met (the API refuses such a habit).
 */
inline bool meets(const Habit& h, double value) {
    if (!h.source || h.source->def == nullptr) {
        return false;
    }
    if (!h.goal) {
        return h.source->def->presence;
    }
    if (std::isnan(value)) {
        return false;
    }
    return h.direction == Direction::AtMost ? value <= *h.goal : value >= *h.goal;
}

/// The catalog ids @p habits read, each once, sorted: habits sharing a source
/// have it read once per window.
inline std::vector<std::string> distinct_sources(const std::vector<Habit>& habits) {
    std::set<std::string> ids;
    for (const auto& h : habits) {
        if (h.source && h.source->def != nullptr) {
            ids.insert(Metrics::canonical(*h.source));
        }
    }
    return {ids.begin(), ids.end()};
}

/// Manual checks: day -> the optional value of the check.
using Checks = std::map<Day, std::optional<double>>;

/// Whether a manual check counts: always without a target, else when its value
/// (the sum of the day) reaches the target.
inline bool check_counts(const Habit& h, const std::optional<double>& value) {
    return !h.target || (value && *value >= *h.target);
}
/// Source values: day -> the value of the habit's source that day.
using Values = std::map<Day, double>;

struct DayState {
    bool done = false;
    bool manual = false;
    std::optional<double> value;  // the check's own value, else the source's
};

inline DayState day_state(const Habit& h, Day day, const Checks& manual, const Values& autos) {
    DayState s;
    const auto a = autos.find(day);
    if (a != autos.end()) {
        if (!std::isnan(a->second)) {
            s.value = a->second;
        }
        s.done = meets(h, a->second);
    }
    const auto m = manual.find(day);
    if (m != manual.end()) {
        s.manual = true;
        s.done = s.done || check_counts(h, m->second);
        if (m->second) {
            s.value = m->second;
        }
    }
    return s;
}

inline std::set<Day> done_days(const Habit& h, const Checks& manual, const Values& autos) {
    std::set<Day> out;
    for (const auto& [day, value] : manual) {
        if (check_counts(h, value)) {
            out.insert(day);
        }
    }
    for (const auto& [day, value] : autos) {
        if (meets(h, value)) {
            out.insert(day);
        }
    }
    return out;
}

inline Day monday_of(Day d) {
    const unsigned iso = std::chrono::weekday{d}.iso_encoding();  // Monday 1 .. Sunday 7
    return d - std::chrono::days{iso - 1};
}

/// Done days in the Monday-to-Sunday week of @p any_day.
inline int week_count(const std::set<Day>& done, Day any_day) {
    const Day from = monday_of(any_day);
    const Day to = from + std::chrono::days{6};
    return static_cast<int>(std::distance(done.lower_bound(from), done.upper_bound(to)));
}

namespace detail {

/// The Mondays of the weeks with at least @p per_week done days.
inline std::set<Day> met_weeks(const std::set<Day>& done, int per_week) {
    std::map<Day, int> counts;
    for (const Day d : done) {
        ++counts[monday_of(d)];
    }
    std::set<Day> out;
    for (const auto& [monday, n] : counts) {
        if (n >= per_week) {
            out.insert(monday);
        }
    }
    return out;
}

/// The run of consecutive members ending at @p last, stepping back by @p step.
inline int run_back(const std::set<Day>& members, Day last, std::chrono::days step) {
    int n = 0;
    for (Day d = last; members.count(d) != 0; d -= step) {
        ++n;
    }
    return n;
}

/// The longest run of members @p step apart.
inline int longest_run(const std::set<Day>& members, std::chrono::days step) {
    int best = 0;
    int run = 0;
    std::optional<Day> prev;
    for (const Day d : members) {
        run = (prev && d - *prev == step) ? run + 1 : 1;
        best = std::max(best, run);
        prev = d;
    }
    return best;
}

}  // namespace detail

/// The current streak on @p today: days for a daily habit, weeks for a weekly one.
inline int streak(const Habit& h, const std::set<Day>& done, Day today) {
    if (h.schedule == Schedule::Daily) {
        const int before = detail::run_back(done, today - std::chrono::days{1}, std::chrono::days{1});
        return before + (done.count(today) != 0 ? 1 : 0);
    }
    const auto met = detail::met_weeks(done, h.per_week);
    const Day this_week = monday_of(today);
    const int before = detail::run_back(met, this_week - std::chrono::days{7}, std::chrono::days{7});
    return before + (met.count(this_week) != 0 ? 1 : 0);
}

/// The longest streak in @p done.
inline int best(const Habit& h, const std::set<Day>& done) {
    if (h.schedule == Schedule::Daily) {
        return detail::longest_run(done, std::chrono::days{1});
    }
    return detail::longest_run(detail::met_weeks(done, h.per_week), std::chrono::days{7});
}

// ── the Now plate ───────────────────────────────────────────────────────────

struct PlateItem {
    std::string name;
    Schedule schedule = Schedule::Daily;
    int per_week = 0;
    bool done_today = false;
    int week_done = 0;  // weekly only: done days this week
};

/// {done, total, left: [names of open daily habits], week: [{name, done, per_week}]}.
/// A weekly habit counts as done for the day when done today or its week is already met.
inline json plate(const std::vector<PlateItem>& items) {
    int done = 0;
    json left = json::array();
    json week = json::array();
    for (const auto& i : items) {
        if (i.schedule == Schedule::Daily) {
            if (i.done_today) {
                ++done;
            } else {
                left.push_back(i.name);
            }
        } else {
            if (i.done_today || i.week_done >= i.per_week) {
                ++done;
            }
            week.push_back({{"name", i.name}, {"done", i.week_done}, {"per_week", i.per_week}});
        }
    }
    return {{"done", done}, {"total", items.size()}, {"left", left}, {"week", week}};
}

}  // namespace Habits
