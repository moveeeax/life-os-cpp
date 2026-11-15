/**
 * @file AniLevel.hpp
 * @brief Ani's level: points for the days of a person's life by four scales
 *        (Tasks, Body, Food, Money) and the level each scale and Ani herself
 *        reaches. Pure rules over the facts of local days; reading the facts
 *        is repositories/agent/LevelRepository.hpp.
 *
 * Every scale gives up to 100 points a day for hitting the person's own goal,
 * not for how much was done. Nothing is taken away: points and levels only
 * grow. A scale with a debt of attention earns half for the days the debt
 * held. Ani's level is the lowest of the four scales' levels.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Domain::AniLevel {

/// The four scales, in the order the app lists them when levels are equal.
enum Scale { kTasks = 0, kBody = 1, kFood = 2, kMoney = 3 };
inline constexpr int kScales = 4;
inline constexpr std::array<const char*, kScales> kScaleIds = {"tasks", "body", "food", "money"};

/// What one local day holds, as read from the person's own records.
struct Day {
    std::string date;  ///< YYYY-MM-DD
    std::string week;  ///< the Monday of its week, YYYY-MM-DD
    int days_in_month = 30;

    // Tasks
    int due = 0;           ///< tasks due that day
    int closed = 0;        ///< of them done by the end of it
    int overdue = 0;       ///< tasks past due at the end of it
    int overdue_long = 0;  ///< of them more than 3 days past due
    int focus_minutes = 0;
    bool focus_done = false;  ///< the day's focus closed

    // Body
    int sleep_minutes = 0;
    int steps = 0;
    int workouts = 0;
    bool measured = false;   ///< weighed or taped
    bool band_data = false;  ///< the band sent sleep or steps

    // Food
    int entries = 0;
    double kcal = 0;
    double protein_g = 0;
    int water_ml = 0;

    // Money
    std::map<std::string, double> spent;  ///< expense by currency
    bool category_over = false;           ///< a category past its monthly budget by that day
};

/// The person's goals the rules measure against.
struct Targets {
    int steps = 8000;
    std::optional<int> kcal;
    std::optional<int> protein_g;
    int water_ml = 2000;
    /// Sum of the categories' monthly budgets by currency; empty when none is set.
    std::map<std::string, double> monthly_budget;
};

/// One day's points of one scale, before a debt halves them.
inline int tasks_points(const Day& d) {
    int p = d.focus_done ? 30 : 0;
    p += std::min(30, 10 * (d.focus_minutes / 25));
    if (d.due > 0) {
        p += static_cast<int>(std::lround(30.0 * d.closed / d.due));
        if (d.overdue == 0) {
            p += 10;
        }
    }
    return p;
}

inline int food_points(const Day& d, const Targets& t) {
    int p = d.entries >= 3 ? 40 : d.entries >= 1 ? 20 : 0;
    if (d.entries > 0 && t.kcal && *t.kcal > 0) {
        const double off = std::fabs(d.kcal - *t.kcal) / *t.kcal;
        p += off <= 0.10 ? 25 : off <= 0.20 ? 12 : 0;
    }
    if (t.protein_g && *t.protein_g > 0) {
        p += d.protein_g >= *t.protein_g ? 20 : d.protein_g >= 0.8 * *t.protein_g ? 10 : 0;
    }
    if (t.water_ml > 0) {
        p += d.water_ml >= t.water_ml ? 15 : d.water_ml * 2 >= t.water_ml ? 7 : 0;
    }
    return p;
}

/// Sleep and steps only; the workout's 30 and the weekly measure are laid by score().
inline int body_points(const Day& d, const Targets& t) {
    int p = d.sleep_minutes >= 420 ? 40 : d.sleep_minutes >= 360 ? 20 : 0;
    p += d.steps >= t.steps ? 30 : d.steps >= 0.8 * t.steps ? 15 : 0;
    return p;
}

/// Money: spending within the day's share of the monthly budget (per currency),
/// no category past its budget, and every spending categorized (an expense
/// always has a category, so that part is always earned). Without a budget
/// only that last part can be.
inline int money_points(const Day& d, const Targets& t) {
    int p = 20;
    if (t.monthly_budget.empty()) {
        return p;
    }
    bool within = true;
    for (const auto& [currency, budget] : t.monthly_budget) {
        const auto spent = d.spent.find(currency);
        if (spent != d.spent.end() && spent->second > budget / std::max(1, d.days_in_month)) {
            within = false;
        }
    }
    if (within) {
        p += 50;
    }
    if (!d.category_over) {
        p += 30;
    }
    return p;
}

/// Points and the state of one scale after all the days.
struct ScaleResult {
    long points = 0;
    long week = 0;      ///< the last 7 days
    bool debt = false;  ///< the debt of attention holds on the last day
};

struct Result {
    std::array<ScaleResult, kScales> scales;
};

/// Total points that reach level @p level (level 1 is 0): 100·(L−1) + 25·(L−1)·L.
inline long threshold(int level) {
    const long n = std::max(1, level) - 1;
    return 100 * n + 25 * n * (n + 1);
}

/// The level @p points reach.
inline int level_of(long points) {
    int level = 1;
    while (threshold(level + 1) <= points) {
        ++level;
    }
    return level;
}

/// Ani's level: the lowest of the scales'.
inline int overall(const Result& r) {
    int level = level_of(r.scales[0].points);
    for (const auto& s : r.scales) {
        level = std::min(level, level_of(s.points));
    }
    return level;
}

/// Points of every scale over @p days (oldest first, one per local day).
inline Result score(const std::vector<Day>& days, const Targets& t) {
    Result out;
    std::map<std::string, int> workout_days;  // per week, days that took a workout point
    std::map<std::string, bool> measured_week;
    for (size_t i = 0; i < days.size(); ++i) {
        const Day& d = days[i];
        std::array<int, kScales> p = {tasks_points(d), body_points(d, t), food_points(d, t), money_points(d, t)};

        // Body: a workout is worth 30, four days a week at most; the first
        // measure of a week is worth 20 on top.
        if (d.workouts > 0 && workout_days[d.week] < 4) {
            ++workout_days[d.week];
            p[kBody] += 30;
        }

        std::array<bool, kScales> debt = {d.overdue_long > 0, false, false, false};
        const auto quiet = [&](size_t back, auto&& silent) {
            if (i < back) {
                return false;
            }
            for (size_t k = 0; k <= back; ++k) {
                if (!silent(days[i - k])) {
                    return false;
                }
            }
            return true;
        };
        debt[kFood] = quiet(1, [](const Day& x) { return x.entries == 0; });
        debt[kBody] = quiet(2, [](const Day& x) { return !x.band_data; });

        for (int s = 0; s < kScales; ++s) {
            int points = debt[s] ? p[s] / 2 : p[s];
            if (s == kBody && d.measured && !measured_week[d.week]) {
                measured_week[d.week] = true;
                points += 20;
            }
            out.scales[s].points += points;
            if (i + 7 >= days.size()) {
                out.scales[s].week += points;
            }
            if (i + 1 == days.size()) {
                out.scales[s].debt = debt[s];
            }
        }
    }
    return out;
}

}  // namespace Domain::AniLevel
