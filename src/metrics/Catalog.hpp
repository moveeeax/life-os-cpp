/**
 * @file Catalog.hpp
 * @brief The catalog of daily series a number goal or a habit can read
 *        (docs/superpowers/specs/2026-10-20-metric-sources-design.md): one
 *        value per local day, grouped by the section it comes from. Pure: no
 *        database. An id is `<module>:<metric>`, some with parameters: a
 *        currency (`money:expense:KZT`), a currency and a category
 *        (`money:expense:KZT:<uuid>`), a habit (`habits:<uuid>`). A Def's id is
 *        the template, with `{currency}`, `{category}` and `{habit}` standing
 *        for the parameters.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Metrics {

/// How the rows of one day become its value.
enum class Reduce { Sum, Last, Avg, Min, Max, Count };
/// The parameters the id carries after the metric.
enum class Param { None, Currency, CurrencyCategory, Habit };
/// What the caller needs for the series: the module switch, and for Mi the
/// fitness read permission and a linked Mi account.
enum class Gate { Mi, Tape, Food, Habits, Workout, Tasks, Money, Practice };

struct Def {
    std::string_view id;      // the template: "fitness:steps", "money:expense:{currency}"
    std::string_view module;  // the group: the id's first segment
    std::string_view label;
    std::string_view unit;  // empty for a currency (the unit is the currency) and a habit (its own unit)
    Reduce reduce;
    /// A day with a value counts by itself (a weigh-in, a mood, a session): a habit
    /// on it needs no goal. The others need a goal to compare with.
    bool presence;
    Param param;
    Gate gate;
};

/// Every entry of the spec, in spec order.
inline const std::vector<Def>& catalog() {
    constexpr auto Sum = Reduce::Sum;
    constexpr auto Last = Reduce::Last;
    constexpr auto Avg = Reduce::Avg;
    constexpr auto Min = Reduce::Min;
    constexpr auto Max = Reduce::Max;
    constexpr auto Count = Reduce::Count;
    static const std::vector<Def> kAll = {
        // Mi daily activity: the date of the row, summed over devices.
        {"fitness:steps", "fitness", "Steps", "steps", Sum, false, Param::None, Gate::Mi},
        {"fitness:distance", "fitness", "Distance", "m", Sum, false, Param::None, Gate::Mi},
        {"fitness:active_kcal", "fitness", "Active calories", "kcal", Sum, false, Param::None, Gate::Mi},
        {"fitness:total_kcal", "fitness", "Total calories", "kcal", Sum, false, Param::None, Gate::Mi},
        {"fitness:floors", "fitness", "Floors", "floors", Sum, false, Param::None, Gate::Mi},
        {"fitness:active_minutes", "fitness", "Active minutes", "min", Sum, false, Param::None, Gate::Mi},
        // The night (no nap) by its local end day, the latest ending one. A night is
        // there every day the band is worn, so it needs a goal: no presence.
        {"fitness:sleep", "fitness", "Sleep", "h", Last, false, Param::None, Gate::Mi},
        {"fitness:sleep_duration", "fitness", "Time in bed", "h", Last, false, Param::None, Gate::Mi},
        {"fitness:sleep_awake", "fitness", "Awake at night", "min", Last, false, Param::None, Gate::Mi},
        {"fitness:sleep_score", "fitness", "Sleep score", "", Last, false, Param::None, Gate::Mi},
        // The scales, the last record of the day.
        {"fitness:weight", "fitness", "Weight", "kg", Last, true, Param::None, Gate::Mi},
        {"fitness:bmi", "fitness", "BMI", "", Last, true, Param::None, Gate::Mi},
        {"fitness:body_fat", "fitness", "Body fat", "%", Last, true, Param::None, Gate::Mi},
        {"fitness:muscle", "fitness", "Muscle mass", "kg", Last, true, Param::None, Gate::Mi},
        {"fitness:water", "fitness", "Body water", "%", Last, true, Param::None, Gate::Mi},
        {"fitness:bone", "fitness", "Bone mass", "kg", Last, true, Param::None, Gate::Mi},
        {"fitness:visceral_fat", "fitness", "Visceral fat", "", Last, true, Param::None, Gate::Mi},
        {"fitness:bmr", "fitness", "Basal metabolism", "kcal", Last, true, Param::None, Gate::Mi},
        {"fitness:metabolic_age", "fitness", "Metabolic age", "years", Last, true, Param::None, Gate::Mi},
        // Heart rate: the last resting value; average and peak of the other samples.
        {"fitness:resting_hr", "fitness", "Resting heart rate", "bpm", Last, true, Param::None, Gate::Mi},
        {"fitness:hr_avg", "fitness", "Average heart rate", "bpm", Avg, false, Param::None, Gate::Mi},
        {"fitness:hr_max", "fitness", "Peak heart rate", "bpm", Max, false, Param::None, Gate::Mi},
        {"fitness:spo2", "fitness", "Blood oxygen", "%", Avg, false, Param::None, Gate::Mi},
        {"fitness:spo2_min", "fitness", "Lowest blood oxygen", "%", Min, false, Param::None, Gate::Mi},
        {"fitness:stress", "fitness", "Stress", "", Avg, false, Param::None, Gate::Mi},
        {"fitness:stress_max", "fitness", "Peak stress", "", Max, false, Param::None, Gate::Mi},
        {"fitness:abnormal_hr", "fitness", "Abnormal heartbeat events", "events", Count, true, Param::None, Gate::Mi},
        // Workouts the band recorded, by their local start day.
        {"fitness:band_workouts", "fitness", "Band workouts", "workouts", Count, true, Param::None, Gate::Mi},
        {"fitness:band_workout_minutes", "fitness", "Band workout time", "min", Sum, false, Param::None, Gate::Mi},
        {"fitness:band_workout_kcal", "fitness", "Band workout calories", "kcal", Sum, false, Param::None, Gate::Mi},
        {"fitness:band_workout_distance", "fitness", "Band workout distance", "m", Sum, false, Param::None, Gate::Mi},
        // The food diary, summed over the day's entries.
        {"food:kcal", "food", "Calories eaten", "kcal", Sum, false, Param::None, Gate::Food},
        {"food:protein", "food", "Protein", "g", Sum, false, Param::None, Gate::Food},
        {"food:fat", "food", "Fat", "g", Sum, false, Param::None, Gate::Food},
        {"food:carbs", "food", "Carbs", "g", Sum, false, Param::None, Gate::Food},
        {"food:fiber", "food", "Fiber", "g", Sum, false, Param::None, Gate::Food},
        {"food:sugar", "food", "Sugar", "g", Sum, false, Param::None, Gate::Food},
        {"food:salt", "food", "Salt", "g", Sum, false, Param::None, Gate::Food},
        // Habits: the mood check-in, and the day value of a habit ticked by hand.
        {"habits:mood", "habits", "Mood", "", Last, true, Param::None, Gate::Habits},
        {"habits:{habit}", "habits", "Habit", "", Last, true, Param::Habit, Gate::Habits},
        // The tape, the last measurement of the day.
        {"tape:waist", "tape", "Waist", "cm", Last, true, Param::None, Gate::Tape},
        {"tape:chest", "tape", "Chest", "cm", Last, true, Param::None, Gate::Tape},
        {"tape:hips", "tape", "Hips", "cm", Last, true, Param::None, Gate::Tape},
        {"tape:neck", "tape", "Neck", "cm", Last, true, Param::None, Gate::Tape},
        {"tape:arm", "tape", "Arm", "cm", Last, true, Param::None, Gate::Tape},
        {"tape:thigh", "tape", "Thigh", "cm", Last, true, Param::None, Gate::Tape},
        // Training, the finished sessions by their local end day.
        {"workout:sessions", "workout", "Training sessions", "sessions", Count, true, Param::None, Gate::Workout},
        {"workout:minutes", "workout", "Training time", "min", Sum, false, Param::None, Gate::Workout},
        {"workout:volume", "workout", "Training volume", "kg", Sum, false, Param::None, Gate::Workout},
        {"workout:sets", "workout", "Work sets", "sets", Count, true, Param::None, Gate::Workout},
        // Tasks completed in the local day.
        {"tasks:done", "tasks", "Tasks done", "tasks", Count, true, Param::None, Gate::Tasks},
        // Money, one currency each, no conversion.
        {"money:expense:{currency}", "money", "Expenses", "", Sum, false, Param::Currency, Gate::Money},
        {"money:income:{currency}", "money", "Income", "", Sum, false, Param::Currency, Gate::Money},
        {"money:expense:{currency}:{category}",
         "money",
         "Expenses in a category",
         "",
         Sum,
         false,
         Param::CurrencyCategory,
         Gate::Money},
        // The end of the day, every day: always there, so no presence.
        {"money:balance:{currency}", "money", "Balance", "", Last, false, Param::Currency, Gate::Money},
        // The practice ledger, one currency each.
        {"practice:income:{currency}", "practice", "Practice income", "", Sum, false, Param::Currency, Gate::Practice},
        {"practice:expense:{currency}",
         "practice",
         "Practice expenses",
         "",
         Sum,
         false,
         Param::Currency,
         Gate::Practice},
    };
    return kAll;
}

/// A parsed id: its catalog entry and its parameters (empty when it has none).
struct Ref {
    const Def* def = nullptr;
    std::string currency;     // "KZT"
    std::string category_id;  // a lower-case uuid
    std::string habit_id;     // a lower-case uuid
};

namespace detail {

inline std::vector<std::string_view> split(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (true) {
        const auto colon = s.find(':', start);
        if (colon == std::string_view::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, colon - start));
        start = colon + 1;
    }
}

inline bool is_currency(std::string_view s) {
    return s.size() == 3 && std::all_of(s.begin(), s.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
}

/// A uuid in the 8-4-4-4-12 hex form, any case.
inline bool is_uuid(std::string_view s) {
    if (s.size() != 36) {
        return false;
    }
    for (std::size_t i = 0; i < s.size(); ++i) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? s[i] != '-' : std::isxdigit(static_cast<unsigned char>(s[i])) == 0) {
            return false;
        }
    }
    return true;
}

inline std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(
        out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// The habit sources stored before the catalog, and the ids they mean now.
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 5> kLegacy = {{
    {"sleep", "fitness:sleep"},
    {"steps", "fitness:steps"},
    {"active_kcal", "fitness:active_kcal"},
    {"workout", "workout:sessions"},
    {"weight", "fitness:weight"},
}};

}  // namespace detail

/// The entry of a template id ("fitness:steps", "money:expense:{currency}"), or null.
inline const Def* find(std::string_view id) {
    for (const auto& d : catalog()) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

/**
 * The entry and parameters of @p source, or nullopt when it names nothing in
 * the catalog. A currency is three upper-case letters; a category or a habit
 * is a uuid (kept lower-case). The five legacy habit sources (sleep, steps,
 * active_kcal, workout, weight) map to their catalog ids; "manual" is not a
 * catalog id.
 */
inline std::optional<Ref> parse(std::string_view source) {
    for (const auto& [old, now] : detail::kLegacy) {
        if (source == old) {
            source = now;
            break;
        }
    }
    const auto parts = detail::split(source);
    for (const auto& d : catalog()) {
        const auto pattern = detail::split(d.id);
        if (pattern.size() != parts.size()) {
            continue;
        }
        Ref r;
        r.def = &d;
        bool ok = true;
        for (std::size_t i = 0; ok && i < parts.size(); ++i) {
            if (pattern[i] == "{currency}") {
                ok = detail::is_currency(parts[i]);
                r.currency = std::string(parts[i]);
            } else if (pattern[i] == "{category}") {
                ok = detail::is_uuid(parts[i]);
                r.category_id = detail::lower(parts[i]);
            } else if (pattern[i] == "{habit}") {
                ok = detail::is_uuid(parts[i]);
                r.habit_id = detail::lower(parts[i]);
            } else {
                ok = pattern[i] == parts[i];
            }
        }
        if (ok) {
            return r;
        }
    }
    return std::nullopt;
}

/// The id of @p r with its parameters filled in: "money:expense:KZT:<uuid>".
inline std::string canonical(const Ref& r) {
    if (r.def == nullptr) {
        return "";
    }
    std::string out;
    bool first = true;
    for (const auto part : detail::split(r.def->id)) {
        if (!first) {
            out += ':';
        }
        first = false;
        if (part == "{currency}") {
            out += r.currency;
        } else if (part == "{category}") {
            out += r.category_id;
        } else if (part == "{habit}") {
            out += r.habit_id;
        } else {
            out += part;
        }
    }
    return out;
}

/// The unit of a series: the entry's, the currency for money and practice. A
/// habit's unit is its own and is not known here (empty).
inline std::string unit_of(const Ref& r) {
    if (r.def == nullptr) {
        return "";
    }
    if (r.def->param == Param::Currency || r.def->param == Param::CurrencyCategory) {
        return r.currency;
    }
    return std::string(r.def->unit);
}

/// The name of @p r in the API: sum, last, avg, min, max, count.
inline std::string_view reduce_name(Reduce r) {
    switch (r) {
        case Reduce::Sum:
            return "sum";
        case Reduce::Last:
            return "last";
        case Reduce::Avg:
            return "avg";
        case Reduce::Min:
            return "min";
        case Reduce::Max:
            return "max";
        case Reduce::Count:
            return "count";
    }
    return "sum";
}

}  // namespace Metrics
