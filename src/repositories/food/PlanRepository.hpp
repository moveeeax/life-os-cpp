/**
 * @file PlanRepository.hpp
 * @brief The meal plan: a template per weekday (food_plan_templates, 1 =
 *        Monday ... 7 = Sunday) and an override for one day (food_plan_days).
 *        Rows belong to the caller.
 *
 * Meals are a JSON array of {time "HH:MM", title, kcal, protein_g?, meal},
 * validated and sorted by the controller. A day reads its override, else its
 * weekday's template, else nothing; each row comes back with `eaten`, true
 * when the diary has an entry of the same meal on that day.
 */

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories {

class PlanRepository {
public:
    /// {source: "day" | "template" | "none", meals: [... with eaten]} of @p date.
    /// @p from_primary forces the primary (read-after-write: the answer to a
    /// PUT or DELETE of the day).
    nlohmann::json day(const std::string& owner, const std::string& date, bool from_primary = false) {
        auto query = [&](auto& txn) {
            auto r = txn.exec_params(
                "WITH d AS (SELECT meals FROM food_plan_days WHERE owner_id = $1::uuid AND day = $2::date), "
                "t AS (SELECT meals FROM food_plan_templates WHERE owner_id = $1::uuid "
                "      AND weekday = EXTRACT(ISODOW FROM $2::date)), "
                "p AS (SELECT CASE WHEN EXISTS (SELECT 1 FROM d) THEN 'day' "
                "                  WHEN EXISTS (SELECT 1 FROM t) THEN 'template' ELSE 'none' END AS source, "
                "             COALESCE((SELECT meals FROM d), (SELECT meals FROM t), '[]'::jsonb) AS meals) "
                "SELECT p.source, COALESCE((SELECT jsonb_agg(m.value || jsonb_build_object('eaten', EXISTS ("
                "    SELECT 1 FROM food_entries e WHERE e.owner_id = $1::uuid AND e.date = $2::date "
                "    AND e.meal = m.value->>'meal')) ORDER BY m.value->>'time', m.ord) "
                "  FROM jsonb_array_elements(p.meals) WITH ORDINALITY AS m(value, ord)), '[]'::jsonb)::text "
                "FROM p",
                owner,
                date);
            if (r.empty()) {
                return nlohmann::json{{"source", "none"}, {"meals", nlohmann::json::array()}};
            }
            return nlohmann::json{{"source", r[0][0].template as<std::string>()},
                                  {"meals", nlohmann::json::parse(r[0][1].template as<std::string>())}};
        };
        return from_primary ? Database::get().execute_read_primary(query) : Database::get().execute_read(query);
    }

    /// The seven weekdays in order, [{weekday, meals}]; a weekday without a template has [].
    nlohmann::json templates(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT json_agg(json_build_object('weekday', w, 'meals', COALESCE(t.meals, '[]'::jsonb)) "
                "ORDER BY w)::text FROM generate_series(1, 7) AS w "
                "LEFT JOIN food_plan_templates t ON t.owner_id = $1::uuid AND t.weekday = w",
                owner);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// Replaces the template of @p weekday; an empty @p meals_json array drops it.
    void put_template(const std::string& owner, int weekday, const std::string& meals_json) {
        Database::get().execute_write([&](auto& txn) {
            if (meals_json == "[]") {
                txn.exec_params(
                    "DELETE FROM food_plan_templates WHERE owner_id = $1::uuid AND weekday = $2", owner, weekday);
                return true;
            }
            txn.exec_params(
                "INSERT INTO food_plan_templates (owner_id, weekday, meals) VALUES ($1::uuid, $2, $3::jsonb) "
                "ON CONFLICT (owner_id, weekday) DO UPDATE SET meals = EXCLUDED.meals",
                owner,
                weekday,
                meals_json);
            return true;
        });
    }

    /// Sets the override of @p date; an empty array is a day planned with nothing.
    void put_day(const std::string& owner, const std::string& date, const std::string& meals_json) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO food_plan_days (owner_id, day, meals) VALUES ($1::uuid, $2::date, $3::jsonb) "
                "ON CONFLICT (owner_id, day) DO UPDATE SET meals = EXCLUDED.meals",
                owner,
                date,
                meals_json);
            return true;
        });
    }

    /// Drops the override of @p date (none is fine): the weekday's template applies again.
    void remove_day(const std::string& owner, const std::string& date) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params("DELETE FROM food_plan_days WHERE owner_id = $1::uuid AND day = $2::date", owner, date);
            return true;
        });
    }
};

}  // namespace Repositories
