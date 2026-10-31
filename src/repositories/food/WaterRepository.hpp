/**
 * @file WaterRepository.hpp
 * @brief Water drunk per local day and the day's goal (food_water, and
 *        food_goals.water_ml; 2000 ml when unset). Rows belong to the caller.
 */

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories {

class WaterRepository {
public:
    static constexpr int kDefaultGoalMl = 2000;
    static constexpr int kMaxDayMl = 20000;

    /// {date, ml, goal_ml} of @p date.
    nlohmann::json day(const std::string& owner, const std::string& date) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE((SELECT ml FROM food_water WHERE owner_id = $1::uuid AND day = $2::date), 0), "
                "COALESCE((SELECT water_ml FROM food_goals WHERE owner_id = $1::uuid), $3)",
                owner,
                date,
                kDefaultGoalMl);
            return nlohmann::json{
                {"date", date}, {"ml", r[0][0].template as<int>()}, {"goal_ml", r[0][1].template as<int>()}};
        });
    }

    /// Adds @p delta_ml (negative takes back) to @p date, kept between 0 and 20 000 ml; the day after it.
    nlohmann::json add(const std::string& owner, const std::string& date, int delta_ml) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO food_water (owner_id, day, ml) VALUES ($1::uuid, $2::date, LEAST($4, GREATEST(0, $3))) "
                "ON CONFLICT (owner_id, day) DO UPDATE SET ml = LEAST($4, GREATEST(0, food_water.ml + $3))",
                owner,
                date,
                delta_ml,
                kMaxDayMl);
            return true;
        });
        return day(owner, date);
    }

    /// The day's goal; the goals row is made when there is none yet.
    void set_goal(const std::string& owner, int ml) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO food_goals (owner_id, water_ml) VALUES ($1::uuid, $2) ON CONFLICT (owner_id) DO UPDATE "
                "SET water_ml = EXCLUDED.water_ml",
                owner,
                ml);
            return true;
        });
    }
};

}  // namespace Repositories
