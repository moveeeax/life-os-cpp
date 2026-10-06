/**
 * @file Goals.hpp
 * @brief Daily calorie and macro goals from the user's profile.
 *
 * BMR by Mifflin-St Jeor (Mifflin MD et al., Am J Clin Nutr 1990; 51(2):
 * 241-247), maintenance by the usual activity factors, a deficit from the
 * weekly pace at about 7700 kcal per kilogram (a rule of thumb). The calorie
 * goal never goes below kMinKcal; a goal below the BMR is allowed and flagged
 * (the owner's own target is below theirs). Macros: protein per kilogram of the target
 * weight, fat as a share of the calories, carbohydrates the rest. Every
 * number can be overridden by the user; the overrides replace one number
 * each and nothing is re-derived from them.
 *
 * Pure arithmetic: no database, no config.
 */

#pragma once

#include <chrono>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Food::Goals {

/// Lowest calorie goal the arithmetic produces; below it the page should warn.
inline constexpr int kMinKcal = 1200;
inline constexpr double kKcalPerKg = 7700.0;
inline constexpr double kProteinPerKg = 1.8;
inline constexpr double kFatShare = 0.25;
inline constexpr double kKcalPerGramProtein = 4.0;
inline constexpr double kKcalPerGramFat = 9.0;
inline constexpr double kKcalPerGramCarbs = 4.0;

struct Inputs {
    double weight_kg = 0;
    int height_cm = 0;
    int age_years = 0;
    std::string sex;       // male | female
    std::string activity;  // sedentary | light | moderate | active | very_active
    double target_weight_kg = 0;
    double pace_kg_per_week = 0;  // positive loses, negative gains
};

struct Computed {
    int bmr = 0;
    int maintenance = 0;
    int deficit = 0;
    int kcal = 0;
    /// The deficit took the goal below kMinKcal; the goal is kMinKcal.
    bool floored = false;
    /// The goal is below the BMR: worth a warning, not forbidden.
    bool below_bmr = false;
    int protein_g = 0;
    int fat_g = 0;
    int carbs_g = 0;
};

struct Targets {
    int kcal = 0;
    int protein_g = 0;
    int fat_g = 0;
    int carbs_g = 0;
};

/// 1.2 … 1.9 for the five levels. @throws std::invalid_argument for anything else.
inline double activity_factor(std::string_view activity) {
    if (activity == "sedentary")
        return 1.2;
    if (activity == "light")
        return 1.375;
    if (activity == "moderate")
        return 1.55;
    if (activity == "active")
        return 1.725;
    if (activity == "very_active")
        return 1.9;
    throw std::invalid_argument("unknown activity level");
}

namespace detail {

inline int round_half_up(double v) {
    return static_cast<int>(std::floor(v + 0.5));
}

inline std::chrono::year_month_day parse_ymd(std::string_view text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        throw std::invalid_argument("date must be YYYY-MM-DD");
    }
    const auto num = [&](std::size_t at, std::size_t len) {
        int v = 0;
        for (std::size_t i = at; i < at + len; ++i) {
            if (text[i] < '0' || text[i] > '9') {
                throw std::invalid_argument("date must be YYYY-MM-DD");
            }
            v = v * 10 + (text[i] - '0');
        }
        return v;
    };
    const std::chrono::year_month_day ymd{std::chrono::year(num(0, 4)),
                                          std::chrono::month(static_cast<unsigned>(num(5, 2))),
                                          std::chrono::day(static_cast<unsigned>(num(8, 2)))};
    if (!ymd.ok()) {
        throw std::invalid_argument("date is not a calendar day");
    }
    return ymd;
}

}  // namespace detail

/// Whole years between a birth date and a day, both YYYY-MM-DD.
/// @throws std::invalid_argument for a malformed date or a birth date after @p today.
inline int age_on(std::string_view birth_date, std::string_view today) {
    const auto born = detail::parse_ymd(birth_date);
    const auto now = detail::parse_ymd(today);
    if (std::chrono::sys_days(born) > std::chrono::sys_days(now)) {
        throw std::invalid_argument("birth date is in the future");
    }
    int years = static_cast<int>(now.year()) - static_cast<int>(born.year());
    const bool before_birthday = now.month() < born.month() || (now.month() == born.month() && now.day() < born.day());
    if (before_birthday) {
        --years;
    }
    return years;
}

/// @throws std::invalid_argument for an unknown sex or activity.
inline Computed compute(const Inputs& in) {
    if (in.sex != "male" && in.sex != "female") {
        throw std::invalid_argument("sex must be male or female");
    }
    Computed out;
    const double bmr_raw =
        10.0 * in.weight_kg + 6.25 * in.height_cm - 5.0 * in.age_years + (in.sex == "male" ? 5.0 : -161.0);
    out.bmr = detail::round_half_up(bmr_raw);
    out.maintenance = detail::round_half_up(out.bmr * activity_factor(in.activity));
    out.deficit = detail::round_half_up(in.pace_kg_per_week * kKcalPerKg / 7.0);
    out.kcal = out.maintenance - out.deficit;
    if (out.kcal < kMinKcal) {
        out.kcal = kMinKcal;
        out.floored = true;
    }
    out.below_bmr = out.kcal < out.bmr;
    out.protein_g = detail::round_half_up(kProteinPerKg * in.target_weight_kg);
    out.fat_g = detail::round_half_up(kFatShare * out.kcal / kKcalPerGramFat);
    const double carbs_kcal = out.kcal - out.protein_g * kKcalPerGramProtein - out.fat_g * kKcalPerGramFat;
    out.carbs_g = carbs_kcal > 0 ? detail::round_half_up(carbs_kcal / kKcalPerGramCarbs) : 0;
    return out;
}

/// The goals after the overrides: each override replaces its own number only.
inline Targets apply_overrides(const Computed& c,
                               std::optional<int> kcal,
                               std::optional<int> protein_g,
                               std::optional<int> fat_g,
                               std::optional<int> carbs_g) {
    return {
        kcal.value_or(c.kcal), protein_g.value_or(c.protein_g), fat_g.value_or(c.fat_g), carbs_g.value_or(c.carbs_g)};
}

}  // namespace Food::Goals
