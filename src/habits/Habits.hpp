/**
 * @file Habits.hpp
 * @brief The habit evaluation (docs/superpowers/specs/2026-10-14-habits-design.md).
 *        Pure: no database, no clock; "today" is always passed in.
 *
 *        A day is done when it has a manual check (with a daily target, one
 *        whose value, the sum of the day, reaches the target) or the habit's
 *        source meets the goal that day: sleep minutes >= goal x 60, steps >=
 *        goal, active kcal >= goal, a workout when a session finished that
 *        local day, a weigh-in when the Mi scales took a record that local
 *        day. A daily habit streaks over consecutive done days ending
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
enum class Source { Manual, Sleep, Steps, ActiveKcal, Workout, Weight };

inline std::optional<Schedule> schedule_of(std::string_view s) {
    if (s == "daily") {
        return Schedule::Daily;
    }
    if (s == "weekly") {
        return Schedule::Weekly;
    }
    return std::nullopt;
}

inline std::optional<Source> source_of(std::string_view s) {
    if (s == "manual") {
        return Source::Manual;
    }
    if (s == "sleep") {
        return Source::Sleep;
    }
    if (s == "steps") {
        return Source::Steps;
    }
    if (s == "active_kcal") {
        return Source::ActiveKcal;
    }
    if (s == "workout") {
        return Source::Workout;
    }
    if (s == "weight") {
        return Source::Weight;
    }
    return std::nullopt;
}

/// Whether the source reads the Mi Fitness tables (as opposed to Training or nothing).
inline bool from_health(Source s) {
    return s == Source::Sleep || s == Source::Steps || s == Source::ActiveKcal || s == Source::Weight;
}

struct Habit {
    Schedule schedule = Schedule::Daily;
    int per_week = 0;  // weekly only, 1..7
    std::optional<double> goal;
    Source source = Source::Manual;
    /// A hand-ticked habit's daily target: a check counts only when its value reaches it.
    std::optional<double> target;
};

/// Whether a day's source value meets the goal. Sleep values are minutes, the goal hours;
/// a workout value is the count of sessions and ignores the goal; a weigh-in is met by
/// being there at all, whatever it weighs (kNoNumber included).
inline bool meets(const Habit& h, double value) {
    switch (h.source) {
        case Source::Manual:
            return false;
        case Source::Workout:
            return value >= 1;
        case Source::Weight:
            return true;
        case Source::Sleep:
            return h.goal.has_value() && value >= *h.goal * 60.0;
        case Source::Steps:
        case Source::ActiveKcal:
            return h.goal.has_value() && value >= *h.goal;
    }
    return false;
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

/// The value of a day that has a record but no number in it: a weigh-in whose
/// weight_kg is null. The day counts (meets() looks at the record, not the number)
/// and day_state() reports its value as null.
inline constexpr double kNoNumber = std::numeric_limits<double>::quiet_NaN();

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

// ── sources on the local day ────────────────────────────────────────────────

/// The local day of a UTC instant "YYYY-MM-DDTHH:MM:SS..." shifted by @p offset_seconds.
inline Day local_day(std::string_view utc, long long offset_seconds) {
    using namespace std::chrono;
    const auto num = [&](std::size_t at) { return (utc[at] - '0') * 10 + (utc[at + 1] - '0'); };
    const sys_seconds at =
        Day{Utils::Date::parse_ymd(utc.substr(0, 10))} + hours{num(11)} + minutes{num(14)} + seconds{num(17)};
    return floor<days>(at + seconds{offset_seconds});
}

/// Asleep minutes by the local day the night ends on (the region zone, @p offset_seconds),
/// as /today picks it: naps left out, the latest ending night of the day kept.
/// @p rows are HealthReadRepository::sleep rows.
inline Values sleep_by_local_day(const json& rows, long long offset_seconds) {
    std::map<Day, std::pair<std::string, double>> latest;
    for (const auto& r : rows) {
        const bool nap = r.contains("is_nap") && r["is_nap"].is_boolean() && r["is_nap"].get<bool>();
        if (nap || !r.contains("end_at") || !r["end_at"].is_string() || !r.contains("time_asleep_minutes") ||
            !r["time_asleep_minutes"].is_number()) {
            continue;
        }
        const std::string end = r["end_at"].get<std::string>();
        if (end.size() < 19) {
            continue;
        }
        const Day day = local_day(end, offset_seconds);
        const auto it = latest.find(day);
        if (it == latest.end() || end > it->second.first) {
            latest[day] = {end, r["time_asleep_minutes"].get<double>()};
        }
    }
    Values out;
    for (const auto& [day, v] : latest) {
        out[day] = v.second;
    }
    return out;
}

/// A daily activity field (steps, active_kcal) by its date; @p rows are
/// HealthReadRepository::daily_activity rows.
inline Values activity_by_day(const json& rows, const char* field) {
    Values out;
    for (const auto& r : rows) {
        if (r.contains("date") && r["date"].is_string() && r.contains(field) && r[field].is_number()) {
            out[day_of(r["date"].get<std::string>())] = r[field].get<double>();
        }
    }
    return out;
}

/// A finished Training session: its UTC end and the zone's offset at that instant.
struct Session {
    std::string finished_at;  // "YYYY-MM-DDTHH:MM:SSZ"
    long long offset_seconds = 0;
};

/// Sessions by the local day they finished on.
inline Values workouts_by_local_day(const std::vector<Session>& sessions) {
    Values out;
    for (const auto& s : sessions) {
        out[local_day(s.finished_at, s.offset_seconds)] += 1;
    }
    return out;
}

/// A weigh-in by the local day it was taken on (the region zone, @p offset_seconds, as
/// sleep): the weight_kg of the day's latest record, kNoNumber when that record has
/// none. @p rows are HealthReadRepository::body rows.
inline Values weight_by_local_day(const json& rows, long long offset_seconds) {
    std::map<Day, std::pair<std::string, double>> latest;
    for (const auto& r : rows) {
        if (!r.contains("timestamp") || !r["timestamp"].is_string()) {
            continue;
        }
        const std::string at = r["timestamp"].get<std::string>();
        if (at.size() < 19) {
            continue;
        }
        const Day day = local_day(at, offset_seconds);
        const auto it = latest.find(day);
        // Rows come oldest first: a later row of the same instant wins as well.
        if (it == latest.end() || at >= it->second.first) {
            const bool weighed = r.contains("weight_kg") && r["weight_kg"].is_number();
            latest[day] = {at, weighed ? r["weight_kg"].get<double>() : kNoNumber};
        }
    }
    Values out;
    for (const auto& [day, v] : latest) {
        out[day] = v.second;
    }
    return out;
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
