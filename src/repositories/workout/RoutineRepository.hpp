/**
 * @file RoutineRepository.hpp
 * @brief Routines of the workout module: a named list of exercises with
 *        targets, optionally tied to a weekday. Always scoped to the owner.
 *
 * A routine is written whole: put() replaces its exercise list in one
 * transaction, so the editor never has to diff rows.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/RepoErrors.hpp"

namespace Repositories {

struct RoutineNotFound : NotFoundError {
    RoutineNotFound() : NotFoundError("routine") {}
};

class RoutineRepository {
public:
    /// The owner's routines without their exercises, ordered by weekday.
    nlohmann::json list(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM ("
                " SELECT r.id, r.name, r.weekday, r.position, r.note, "
                "  (SELECT COUNT(*) FROM routine_exercises re WHERE re.routine_id = r.id) AS exercise_count "
                " FROM routines r WHERE r.owner_id = $1::uuid "
                " ORDER BY r.weekday NULLS LAST, r.position, r.name) t",
                owner);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// One routine with its exercises in order, or nullopt.
    std::optional<nlohmann::json> find(const std::string& owner, const std::string& id) {
        return Database::get().execute_read(
            [&](auto& txn) -> std::optional<nlohmann::json> { return find_in(txn, owner, id); });
    }

    /**
     * @brief Create the routine with this id, or replace it and its exercises.
     * @param exercises_json JSON array of objects: exercise_id, target_sets,
     *        target_reps_min, target_reps_max, target_duration_seconds,
     *        rest_seconds, note. Array order is the order in the routine.
     * @throws RoutineNotFound when the id belongs to another owner.
     * @throws ValidationError("unknown_exercise") when an exercise_id is not
     *         a library exercise or one of the owner's.
     */
    nlohmann::json put(const std::string& owner,
                       const std::string& id,
                       const std::string& name,
                       std::optional<int> weekday,
                       const std::string& note,
                       const std::string& exercises_json) {
        return Database::get().execute_write([&](auto& txn) {
            auto existing = txn.exec_params("SELECT owner_id::text FROM routines WHERE id = $1::uuid", id);
            if (!existing.empty() && existing[0][0].template as<std::string>() != owner) {
                throw RoutineNotFound();
            }
            auto unknown = txn.exec_params(
                "SELECT COUNT(*) FROM json_array_elements($1::json) x WHERE NOT EXISTS ("
                " SELECT 1 FROM exercises e WHERE e.id = x.value->>'exercise_id' "
                " AND (e.source = 'library' OR e.owner_id = $2::uuid))",
                exercises_json,
                owner);
            if (unknown[0][0].template as<long>() > 0) {
                throw ValidationError("unknown_exercise", "exercise_id must be a library or own exercise");
            }
            txn.exec_params(
                "INSERT INTO routines (id, owner_id, name, weekday, note) VALUES ($1::uuid, $2::uuid, $3, $4, $5) "
                "ON CONFLICT (id) DO UPDATE SET name = EXCLUDED.name, weekday = EXCLUDED.weekday, "
                "note = EXCLUDED.note",
                id,
                owner,
                name,
                weekday,
                note);
            txn.exec_params("DELETE FROM routine_exercises WHERE routine_id = $1::uuid", id);
            txn.exec_params(
                "INSERT INTO routine_exercises (routine_id, exercise_id, position, target_sets, target_reps_min, "
                "target_reps_max, target_duration_seconds, rest_seconds, note) "
                "SELECT $1::uuid, x.value->>'exercise_id', x.ordinality::int, "
                " COALESCE((x.value->>'target_sets')::int, 3), (x.value->>'target_reps_min')::int, "
                " (x.value->>'target_reps_max')::int, (x.value->>'target_duration_seconds')::int, "
                " COALESCE((x.value->>'rest_seconds')::int, 90), COALESCE(x.value->>'note', '') "
                "FROM json_array_elements($2::json) WITH ORDINALITY AS x(value, ordinality)",
                id,
                exercises_json);
            return *find_in(txn, owner, id);
        });
    }

    /// @throws RoutineNotFound when there is no such routine of this owner.
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM routines WHERE id = $2::uuid AND owner_id = $1::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw RoutineNotFound();
            }
            return 0;
        });
    }

private:
    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params(
            "SELECT row_to_json(t) FROM ("
            " SELECT r.id, r.name, r.weekday, r.position, r.note, "
            "  COALESCE((SELECT json_agg(x ORDER BY x.position) FROM ("
            "    SELECT re.id, re.exercise_id, re.position, re.target_sets, re.target_reps_min, "
            "           re.target_reps_max, re.target_duration_seconds, re.rest_seconds, re.note, "
            "           e.name AS exercise_name, e.tracking_mode, e.images, e.primary_muscles "
            "    FROM routine_exercises re JOIN exercises e ON e.id = re.exercise_id "
            "    WHERE re.routine_id = r.id) x), '[]'::json) AS exercises "
            " FROM routines r WHERE r.id = $2::uuid AND r.owner_id = $1::uuid) t",
            owner,
            id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories
