/**
 * @file Agenda.hpp
 * @brief The agenda of one local day: late, soon (the day and the two after
 *        it), dated, someday, done today, and the two review lists. Pure: the
 *        repository reads the rows with the day and zone already applied.
 */

#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "utils/Date.hpp"

namespace Tasks::Agenda {

inline constexpr int kSoonDays = 2;
inline constexpr int kNoNextStepAfter = 7;
inline constexpr int kStaleAfter = 14;

struct Item {
    std::string id;
    std::string status;        // open | done
    std::string due;           // "" when none
    std::string completed_on;  // the local day it was closed, "" when open
    std::string next_step;
    int age_days = 0;   // days since created, at the agenda date
    int idle_days = 0;  // days since last updated, at the agenda date
    nlohmann::json row;
};

struct Groups {
    std::vector<nlohmann::json> late, soon, dated, someday, done_today, no_next_step, stale;
};

inline Groups group(const std::vector<Item>& items, std::string_view date) {
    using std::chrono::sys_days;
    const sys_days today{Utils::Date::parse_ymd(date)};
    Groups g;
    for (const auto& it : items) {
        if (it.status == "done") {
            if (it.completed_on == date) {
                g.done_today.push_back(it.row);
            }
            continue;
        }
        if (it.due.empty()) {
            g.someday.push_back(it.row);
        } else {
            const auto gap = (sys_days{Utils::Date::parse_ymd(it.due)} - today).count();
            if (gap < 0) {
                g.late.push_back(it.row);
            } else if (gap <= kSoonDays) {
                g.soon.push_back(it.row);
            } else {
                g.dated.push_back(it.row);
            }
        }
        if (it.next_step.empty() && it.age_days > kNoNextStepAfter) {
            g.no_next_step.push_back(it.row);
        }
        if (it.idle_days >= kStaleAfter) {
            g.stale.push_back(it.row);
        }
    }
    return g;
}

}  // namespace Tasks::Agenda
