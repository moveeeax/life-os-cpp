/**
 * @file GoalsRepository.hpp
 * @brief The goals profile of a user (table food_goals) and the weight the
 *        goals are computed from.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories {

class GoalsRepository {
public:
    struct Profile {
        std::optional<int> height_cm;
        std::optional<std::string> birth_date;
        std::optional<std::string> sex;
        std::string activity = "light";
        std::optional<double> target_weight_kg;
        double pace_kg_per_week = 0;
        std::optional<double> manual_weight_kg;
        std::string profile_note;
        std::optional<int> kcal_override;
        std::optional<int> protein_override_g;
        std::optional<int> fat_override_g;
        std::optional<int> carbs_override_g;
    };

    /// The stored profile, or nullopt before the first save.
    std::optional<nlohmann::json> load(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT row_to_json(t) FROM (SELECT " + kColumns + " FROM food_goals WHERE owner_id = $1::uuid) t",
                owner);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// Replace the profile (a full write: the form sends every field).
    nlohmann::json put(const std::string& owner, const Profile& p) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "INSERT INTO food_goals (owner_id, height_cm, birth_date, sex, activity, target_weight_kg, "
                " pace_kg_per_week, manual_weight_kg, profile_note, kcal_override, protein_override_g, "
                " fat_override_g, carbs_override_g) "
                "VALUES ($1::uuid, $2, $3::date, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13) "
                "ON CONFLICT (owner_id) DO UPDATE SET height_cm = EXCLUDED.height_cm, "
                " birth_date = EXCLUDED.birth_date, sex = EXCLUDED.sex, activity = EXCLUDED.activity, "
                " target_weight_kg = EXCLUDED.target_weight_kg, pace_kg_per_week = EXCLUDED.pace_kg_per_week, "
                " manual_weight_kg = EXCLUDED.manual_weight_kg, profile_note = EXCLUDED.profile_note, "
                " kcal_override = EXCLUDED.kcal_override, protein_override_g = EXCLUDED.protein_override_g, "
                " fat_override_g = EXCLUDED.fat_override_g, carbs_override_g = EXCLUDED.carbs_override_g, "
                " updated_at = now() "
                "RETURNING row_to_json((SELECT t FROM (SELECT " +
                    kColumns + ") t))",
                owner,
                p.height_cm,
                p.birth_date,
                p.sex,
                p.activity,
                p.target_weight_kg,
                p.pace_kg_per_week,
                p.manual_weight_kg,
                p.profile_note,
                p.kcal_override,
                p.protein_override_g,
                p.fat_override_g,
                p.carbs_override_g);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// Latest weight of the owner's Mi account, or nullopt without a link or a weigh-in.
    std::optional<double> scale_weight(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<double> {
            auto r = txn.exec_params(
                "SELECT weight_kg FROM body_measurements "
                "WHERE user_id = (SELECT xiaomi_user_id FROM mi_accounts WHERE owner_id = $1::uuid) "
                "ORDER BY timestamp DESC LIMIT 1",
                owner);
            if (r.empty()) {
                return std::nullopt;
            }
            return r[0][0].template as<double>();
        });
    }

    /// Active kcal the band counted on a day of the owner's account, or nullopt.
    std::optional<double> active_kcal(const std::string& owner, const std::string& date) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<double> {
            auto r = txn.exec_params(
                "SELECT SUM(active_kcal) FROM daily_activity "
                "WHERE user_id = (SELECT xiaomi_user_id FROM mi_accounts WHERE owner_id = $1::uuid) "
                "AND date = $2::date AND active_kcal IS NOT NULL",
                owner,
                date);
            if (r.empty() || r[0][0].is_null()) {
                return std::nullopt;
            }
            return r[0][0].template as<double>();
        });
    }

private:
    inline static const std::string kColumns =
        "height_cm, birth_date::text, sex, activity, target_weight_kg, pace_kg_per_week, manual_weight_kg, "
        "profile_note, kcal_override, protein_override_g, fat_override_g, carbs_override_g, "
        "to_char(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS updated_at";
};

}  // namespace Repositories
