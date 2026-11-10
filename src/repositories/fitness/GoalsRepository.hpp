/**
 * @file GoalsRepository.hpp
 * @brief The caller's own fitness goals (fitness_goals, migration 085): the
 *        daily steps goal, 8000 when unset. Keyed by the owner, not by the Mi
 *        account, so unlinking the band keeps it.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories::Fitness {

class GoalsRepository {
public:
    static constexpr int kDefaultSteps = 8000;
    static constexpr int kMinSteps = 1000;
    static constexpr int kMaxSteps = 100000;

    /// The steps goal the owner set, or nullopt.
    std::optional<int> steps_set(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<int> {
            auto r = txn.exec_params("SELECT steps FROM fitness_goals WHERE owner_id = $1::uuid", owner);
            if (r.empty() || r[0][0].is_null()) {
                return std::nullopt;
            }
            return r[0][0].template as<int>();
        });
    }

    /// The steps goal in effect: the owner's, else 8000.
    int steps(const std::string& owner) { return steps_set(owner).value_or(kDefaultSteps); }

    /// {steps, steps_set} of the owner.
    nlohmann::json get(const std::string& owner) {
        const auto set = steps_set(owner);
        return nlohmann::json{{"steps", set.value_or(kDefaultSteps)}, {"steps_set", set.has_value()}};
    }

    /// Sets the steps goal, or clears it back to the default with nullopt.
    void set_steps(const std::string& owner, std::optional<int> steps) {
        Database::get().execute_write([&](auto& txn) {
            if (steps) {
                txn.exec_params(
                    "INSERT INTO fitness_goals (owner_id, steps) VALUES ($1::uuid, $2) ON CONFLICT (owner_id) DO "
                    "UPDATE SET steps = EXCLUDED.steps",
                    owner,
                    *steps);
            } else {
                txn.exec_params("UPDATE fitness_goals SET steps = NULL WHERE owner_id = $1::uuid", owner);
            }
            return true;
        });
    }
};

}  // namespace Repositories::Fitness
