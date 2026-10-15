/**
 * @file Digest.cpp
 * @brief Body for src/food/now/Digest.hpp — compiled once into app_core.
 */

#include "food/now/Digest.hpp"

#include <nlohmann/json.hpp>

#include "repositories/food/EntryRepository.hpp"
#include "repositories/food/GoalsBody.hpp"

namespace Food {

nlohmann::json digest(const std::string& owner, const std::string& day) {
    using json = nlohmann::json;
    const json totals = Repositories::EntryRepository().day(owner, day)["totals"];
    const json targets = Repositories::food_goals_body(owner, day)["targets"];
    const double kcal = totals.value("kcal", 0.0);
    if (kcal == 0.0 && totals.value("protein_g", 0.0) == 0.0 && targets.is_null()) {
        return json();
    }
    json eaten = json::object();
    json target = targets.is_null() ? json() : json::object();
    for (const char* k : {"kcal", "protein_g", "fat_g", "carbs_g"}) {
        eaten[k] = totals.value(k, 0.0);
        if (!targets.is_null()) {
            target[k] = targets.contains(k) ? targets[k] : json();
        }
    }
    return json{{"eaten", eaten}, {"target", target}};
}

}  // namespace Food
