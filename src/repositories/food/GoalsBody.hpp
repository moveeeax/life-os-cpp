/**
 * @file GoalsBody.hpp
 * @brief The food goals block: the profile, the weight with its source, the
 *        computed numbers and the targets after the overrides. Shared by the
 *        food routes and the Now digest (GET /api/v1/today).
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "food/Goals.hpp"
#include "food/Llm.hpp"
#include "repositories/food/GoalsRepository.hpp"

namespace Repositories {

namespace detail {
inline std::optional<int> opt_int_of(const nlohmann::json& body, const std::string& field) {
    if (body.contains(field) && body[field].is_number_integer()) {
        return body[field].get<int>();
    }
    return std::nullopt;
}
}  // namespace detail

/// The goals block of GET/PUT goals and the targets of a day: the profile,
/// the weight with its source, the computed numbers and the targets after
/// the overrides. Targets are null until the profile is complete.
inline nlohmann::json food_goals_body(const std::string& owner, const std::string& today) {
    Repositories::GoalsRepository goals;
    nlohmann::json profile = goals.load(owner).value_or(nlohmann::json{{"height_cm", nullptr},
                                                                       {"birth_date", nullptr},
                                                                       {"sex", nullptr},
                                                                       {"activity", "light"},
                                                                       {"target_weight_kg", nullptr},
                                                                       {"pace_kg_per_week", 0},
                                                                       {"manual_weight_kg", nullptr},
                                                                       {"profile_note", ""},
                                                                       {"kcal_override", nullptr},
                                                                       {"protein_override_g", nullptr},
                                                                       {"fat_override_g", nullptr},
                                                                       {"carbs_override_g", nullptr},
                                                                       {"updated_at", nullptr}});
    nlohmann::json weight{{"kg", nullptr}, {"source", "none"}};
    if (const auto scale = goals.scale_weight(owner)) {
        weight = {{"kg", *scale}, {"source", "scale"}};
    } else if (profile["manual_weight_kg"].is_number()) {
        weight = {{"kg", profile["manual_weight_kg"]}, {"source", "manual"}};
    }

    nlohmann::json missing = nlohmann::json::array();
    for (const char* f : {"height_cm", "birth_date", "sex", "target_weight_kg"}) {
        if (profile[f].is_null()) {
            missing.push_back(f);
        }
    }
    if (weight["kg"].is_null()) {
        missing.push_back("weight");
    }

    nlohmann::json computed;
    nlohmann::json targets;
    if (missing.empty()) {
        Food::Goals::Inputs in;
        in.weight_kg = weight["kg"].get<double>();
        in.height_cm = profile["height_cm"].get<int>();
        in.age_years = Food::Goals::age_on(profile["birth_date"].get<std::string>(), today);
        in.sex = profile["sex"].get<std::string>();
        in.activity = profile["activity"].get<std::string>();
        in.target_weight_kg = profile["target_weight_kg"].get<double>();
        in.pace_kg_per_week = profile["pace_kg_per_week"].get<double>();
        const auto c = Food::Goals::compute(in);
        computed = {{"age_years", in.age_years},
                    {"bmr", c.bmr},
                    {"maintenance", c.maintenance},
                    {"deficit", c.deficit},
                    {"kcal", c.kcal},
                    {"floored", c.floored},
                    {"below_bmr", c.below_bmr},
                    {"protein_g", c.protein_g},
                    {"fat_g", c.fat_g},
                    {"carbs_g", c.carbs_g}};
        const auto t = Food::Goals::apply_overrides(c,
                                                    detail::opt_int_of(profile, "kcal_override"),
                                                    detail::opt_int_of(profile, "protein_override_g"),
                                                    detail::opt_int_of(profile, "fat_override_g"),
                                                    detail::opt_int_of(profile, "carbs_override_g"));
        targets = {{"kcal", t.kcal}, {"protein_g", t.protein_g}, {"fat_g", t.fat_g}, {"carbs_g", t.carbs_g}};
    } else if (profile["kcal_override"].is_number() && profile["protein_override_g"].is_number() &&
               profile["fat_override_g"].is_number() && profile["carbs_override_g"].is_number()) {
        // Everything set by hand: no profile needed.
        targets = {{"kcal", profile["kcal_override"]},
                   {"protein_g", profile["protein_override_g"]},
                   {"fat_g", profile["fat_override_g"]},
                   {"carbs_g", profile["carbs_override_g"]}};
    }
    return nlohmann::json{{"profile", profile},
                          {"weight", weight},
                          {"missing", missing},
                          {"computed", computed},
                          {"targets", targets},
                          // Whether the add form may offer the text parse.
                          {"llm_available", Food::Llm::settings().has_value()}};
}

}  // namespace Repositories
