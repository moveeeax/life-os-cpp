/**
 * @file HabitRepository.hpp
 * @brief habits and habit_checks: the owner's habits and their manual ticks.
 *        The series a sourced habit reads come from repositories/metrics.
 *        Every statement filters by owner; a habit of another owner is not
 *        found.
 */

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "habits/Habits.hpp"
#include "repositories/RepoErrors.hpp"
#include "repositories/SqlErrors.hpp"

namespace Repositories::Habits {

struct NotFound : NotFoundError {
    NotFound() : NotFoundError("habit") {}
};

struct Invariant : ValidationError {
    Invariant() : ValidationError("invariant", "the row breaks a rule of the habits model") {}
};

namespace detail {

using Repositories::detail::translate_sql;

inline void translate(std::string_view sqlstate) {
    if (sqlstate == "23514") {  // check_violation
        throw Invariant();
    }
}

}  // namespace detail

class HabitRepository {
public:
    struct Input {
        std::string name;
        std::string schedule;  // daily | weekly
        std::optional<int> per_week;
        std::optional<double> goal;
        std::string unit;
        std::string source;                  // manual | a catalog id (metrics/Catalog.hpp), canonical
        std::optional<double> target;        // manual only: the amount a day must reach
        std::string direction = "at_least";  // at_least | at_most
    };

    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO habits (owner_id, name, schedule, per_week, goal, unit, source, target, "
                        " direction, position) "
                        "VALUES ($1::uuid, $2, $3, $4, $5, $6, $7, $8, $9, "
                        " (SELECT COALESCE(max(position), 0) + 1 FROM habits WHERE owner_id = $1::uuid)) "
                        "RETURNING id::text",
                        owner,
                        in.name,
                        in.schedule,
                        in.per_week,
                        in.goal,
                        in.unit,
                        in.source,
                        in.target,
                        in.direction);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// A full replacement. @throws NotFound.
    nlohmann::json replace(const std::string& owner, const std::string& id, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE habits SET name = $3, schedule = $4, per_week = $5, goal = $6, unit = $7, source = $8, "
                        " target = $9, direction = $10 WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        in.name,
                        in.schedule,
                        in.per_week,
                        in.goal,
                        in.unit,
                        in.source,
                        in.target,
                        in.direction);
                    if (r.empty()) {
                        throw NotFound();
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// Archive or restore. @throws NotFound.
    nlohmann::json set_archived(const std::string& owner, const std::string& id, bool archived) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "UPDATE habits SET archived_at = CASE WHEN $3::boolean THEN COALESCE(archived_at, now()) END "
                "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                owner,
                id,
                archived);
            if (r.empty()) {
                throw NotFound();
            }
            return *find_in(txn, owner, id);
        });
    }

    /// @throws NotFound. The ticks go with the habit (ON DELETE CASCADE).
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM habits WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw NotFound();
            }
            return true;
        });
    }

    /// The active habits, or the archived ones, in their order.
    nlohmann::json list(const std::string& owner, bool archived) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                    " FROM habits WHERE owner_id = $1::uuid AND (archived_at IS NOT NULL) = $2::boolean "
                    "ORDER BY position, created_at) t",
                owner,
                archived);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// A manual tick on @p date, its value replaced when ticked again. @throws NotFound.
    nlohmann::json put_check(const std::string& owner,
                             const std::string& habit,
                             const std::string& date,
                             std::optional<double> value) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "INSERT INTO habit_checks (owner_id, habit_id, date, value) "
                "SELECT owner_id, id, $3::date, $4 FROM habits WHERE owner_id = $1::uuid AND id = $2::uuid "
                "ON CONFLICT (habit_id, date) DO UPDATE SET value = EXCLUDED.value "
                "RETURNING json_build_object('habit_id', habit_id, 'date', date::text, 'value', value)::text",
                owner,
                habit,
                date,
                value);
            if (r.empty()) {
                throw NotFound();
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// Adds @p amount to the tick of @p date (a tick without a value counts as 0), creating
    /// the tick when there is none; the answer carries the day's sum. @throws NotFound.
    nlohmann::json add_to_check(const std::string& owner,
                                const std::string& habit,
                                const std::string& date,
                                double amount) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "INSERT INTO habit_checks (owner_id, habit_id, date, value) "
                "SELECT owner_id, id, $3::date, $4 FROM habits WHERE owner_id = $1::uuid AND id = $2::uuid "
                "ON CONFLICT (habit_id, date) DO UPDATE SET value = COALESCE(habit_checks.value, 0) + EXCLUDED.value "
                "RETURNING json_build_object('habit_id', habit_id, 'date', date::text, 'value', value)::text",
                owner,
                habit,
                date,
                amount);
            if (r.empty()) {
                throw NotFound();
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// Removes the tick of @p date if there is one. @throws NotFound for a habit that is not the owner's.
    void remove_check(const std::string& owner, const std::string& habit, const std::string& date) {
        Database::get().execute_write([&](auto& txn) {
            auto h = txn.exec_params("SELECT 1 FROM habits WHERE owner_id = $1::uuid AND id = $2::uuid", owner, habit);
            if (h.empty()) {
                throw NotFound();
            }
            txn.exec_params(
                "DELETE FROM habit_checks WHERE owner_id = $1::uuid AND habit_id = $2::uuid AND date = $3::date",
                owner,
                habit,
                date);
            return true;
        });
    }

    /// The manual ticks of the owner's habits dated @p from..@p to (inclusive), by habit id.
    std::map<std::string, ::Habits::Checks> checks(const std::string& owner,
                                                   const std::string& from,
                                                   const std::string& to) {
        return Database::get().execute_read([&](auto& txn) {
            std::map<std::string, ::Habits::Checks> out;
            for (const auto& row :
                 txn.exec_params("SELECT habit_id::text, date::text, value::double precision FROM habit_checks "
                                 "WHERE owner_id = $1::uuid AND date BETWEEN $2::date AND $3::date",
                                 owner,
                                 from,
                                 to)) {
                const auto value = row[2].is_null() ? std::optional<double>() : row[2].template as<double>();
                out[row[0].template as<std::string>()][::Habits::day_of(row[1].template as<std::string>())] = value;
            }
            return out;
        });
    }

    /// Today in the zone @p tz (an IANA name already checked).
    std::string today(const std::string& tz) {
        return Database::get().execute_read([&](auto& txn) {
            return txn.exec_params("SELECT to_char(now() AT TIME ZONE $1, 'YYYY-MM-DD')", tz)[0][0]
                .template as<std::string>();
        });
    }

    /// The owner's habit @p id, active or archived; nullopt when it is not theirs.
    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

private:
    // A source the previous binary wrote under its 1.33 name (the CHECK of migration
    // 056 still takes the five) reads as its catalog id.
    static constexpr const char* kColumns =
        "id, name, schedule, per_week, goal::double precision AS goal, unit, "
        "CASE source WHEN 'sleep' THEN 'fitness:sleep' WHEN 'steps' THEN 'fitness:steps' "
        " WHEN 'active_kcal' THEN 'fitness:active_kcal' WHEN 'workout' THEN 'workout:sessions' "
        " WHEN 'weight' THEN 'fitness:weight' ELSE source END AS source, direction, "
        "target::double precision AS target, position, archived_at, created_at";

    template <typename Txn>
    std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM habits WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Habits
