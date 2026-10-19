/**
 * @file Repeat.hpp
 * @brief The repeat rule of a task and the due date of its next occurrence.
 *        Pure: the repository stores the rule as jsonb and calls next_due
 *        when a repeating task is closed.
 *
 * Four kinds:
 *   {"kind": "daily"}
 *   {"kind": "weekly", "weekdays": [1, 3, 5]}   ISO weekdays, Monday is 1
 *   {"kind": "monthly", "day": 31}               a short month uses its last day
 *   {"kind": "every_n_days", "every": 3}
 *
 * The next occurrence is the first day of the rule strictly after the later
 * of the task's due date and the day it was closed: closing early does not
 * bring the next one closer, closing late does not leave it in the past.
 */

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "utils/Date.hpp"

namespace Tasks::Repeat {

inline constexpr std::array<std::string_view, 4> kKinds{"daily", "weekly", "monthly", "every_n_days"};
inline constexpr int kEveryMax = 365;

struct Rule {
    std::string kind;
    std::vector<int> weekdays;  // weekly: sorted, unique, 1..7
    int day = 0;                // monthly: 1..31
    int every = 0;              // every_n_days: 1..kEveryMax
};

namespace detail {

inline bool small_int(const nlohmann::json& v, int lo, int hi) {
    return v.is_number_integer() && v.get<long long>() >= lo && v.get<long long>() <= hi;
}

}  // namespace detail

/// The rule in @p v into @p out, or the reason it is not one (the API's 400 message).
inline std::optional<std::string> check(const nlohmann::json& v, Rule& out) {
    if (!v.is_object()) {
        return "repeat is an object with a kind, or null";
    }
    if (!v.contains("kind") || !v["kind"].is_string() ||
        std::find(kKinds.begin(), kKinds.end(), v["kind"].get<std::string>()) == kKinds.end()) {
        return "kind is daily, weekly, monthly or every_n_days";
    }
    Rule r;
    r.kind = v["kind"].get<std::string>();
    if (r.kind == "weekly") {
        if (!v.contains("weekdays") || !v["weekdays"].is_array() || v["weekdays"].empty()) {
            return "a weekly rule names its weekdays, 1 (Monday) to 7 (Sunday)";
        }
        for (const auto& d : v["weekdays"]) {
            if (!detail::small_int(d, 1, 7)) {
                return "a weekday is 1 (Monday) to 7 (Sunday)";
            }
            r.weekdays.push_back(d.get<int>());
        }
        std::sort(r.weekdays.begin(), r.weekdays.end());
        r.weekdays.erase(std::unique(r.weekdays.begin(), r.weekdays.end()), r.weekdays.end());
    } else if (r.kind == "monthly") {
        if (!v.contains("day") || !detail::small_int(v["day"], 1, 31)) {
            return "a monthly rule names its day, 1 to 31";
        }
        r.day = v["day"].get<int>();
    } else if (r.kind == "every_n_days") {
        if (!v.contains("every") || !detail::small_int(v["every"], 1, kEveryMax)) {
            return "every is a number of days, 1 to 365";
        }
        r.every = v["every"].get<int>();
    }
    out = std::move(r);
    return std::nullopt;
}

/// The stored form: the kind and only the field it needs.
inline nlohmann::json to_json(const Rule& r) {
    nlohmann::json out{{"kind", r.kind}};
    if (r.kind == "weekly") {
        out["weekdays"] = r.weekdays;
    } else if (r.kind == "monthly") {
        out["day"] = r.day;
    } else if (r.kind == "every_n_days") {
        out["every"] = r.every;
    }
    return out;
}

inline std::string format_ymd(std::chrono::year_month_day d) {
    char buf[16];
    std::snprintf(buf,
                  sizeof buf,
                  "%04d-%02u-%02u",
                  static_cast<int>(d.year()),
                  static_cast<unsigned>(d.month()),
                  static_cast<unsigned>(d.day()));
    return buf;
}

/// The due date of the next occurrence (YYYY-MM-DD); @p due is "" when the task had none.
/// @throws std::invalid_argument when a day is not YYYY-MM-DD.
inline std::string next_due(const Rule& r, std::string_view due, std::string_view done_day) {
    using namespace std::chrono;
    sys_days anchor{Utils::Date::parse_ymd(done_day)};
    if (!due.empty()) {
        anchor = std::max(anchor, sys_days{Utils::Date::parse_ymd(due)});
    }
    if (r.kind == "every_n_days") {
        return format_ymd(year_month_day{anchor + days{r.every}});
    }
    if (r.kind == "weekly" && !r.weekdays.empty()) {
        for (int i = 1; i <= 7; ++i) {
            const sys_days d = anchor + days{i};
            const int wd = static_cast<int>(weekday{d}.iso_encoding());
            if (std::binary_search(r.weekdays.begin(), r.weekdays.end(), wd)) {
                return format_ymd(year_month_day{d});
            }
        }
    }
    if (r.kind == "monthly" && r.day >= 1) {
        const year_month_day a{anchor};
        year_month ym{a.year(), a.month()};
        for (int i = 0; i < 2; ++i, ym += months{1}) {
            const unsigned last =
                static_cast<unsigned>(year_month_day_last{ym.year(), month_day_last{ym.month()}}.day());
            const day d{std::min(static_cast<unsigned>(r.day), last)};
            const sys_days candidate{year_month_day{ym.year(), ym.month(), d}};
            if (candidate > anchor) {
                return format_ymd(year_month_day{candidate});
            }
        }
    }
    return format_ymd(year_month_day{anchor + days{1}});  // daily
}

}  // namespace Tasks::Repeat
