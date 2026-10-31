/**
 * @file DealRepository.hpp
 * @brief ani_deals (migration 075): promises the person makes to Ani for one
 *        week, at most kMaxPerWeek, with their progress read from the rows
 *        that prove them: finished workouts (a session of the Training page,
 *        or a band workout no session claims), focus minutes, a habit's ticks.
 *        The week is Monday to Sunday on the person's clock.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories::Agent {

class DealRepository {
public:
    static constexpr int kMaxPerWeek = 5;

    /// The deals of the week that holds today in @p tz (or of the week starting @p week_start):
    /// [{id, text, kind, habit, target, done, week_start}], oldest first.
    nlohmann::json week(const std::string& owner,
                        const std::string& tz,
                        const std::optional<std::string>& week_start = std::nullopt) {
        return Database::get().execute_read([&](auto& txn) {
            return nlohmann::json::parse(
                txn.exec_params(
                       "WITH w AS (SELECT COALESCE($3::date, date_trunc('week', (now() AT TIME ZONE $2))::date) AS s) "
                       "SELECT COALESCE(json_agg(json_build_object('id', d.id::text, 'text', d.text, 'kind', d.kind, "
                       "'habit', h.name, 'target', d.target, 'week_start', d.week_start, 'done', " +
                           done_sql() +
                           ") ORDER BY d.created_at), '[]'::json)::text FROM ani_deals d CROSS JOIN w LEFT JOIN "
                           "habits h ON h.id = d.habit_id WHERE d.owner_id = $1::uuid AND d.week_start = w.s",
                       owner,
                       tz,
                       week_start)[0][0]
                    .template as<std::string>());
        });
    }

    /// A promise for this week; nullopt when the week holds kMaxPerWeek already or the habit is not theirs.
    std::optional<std::string> add(const std::string& owner,
                                   const std::string& tz,
                                   const std::string& text,
                                   const std::string& kind,
                                   int target,
                                   const std::optional<std::string>& habit_id) {
        return Database::get().execute_write([&](auto& txn) -> std::optional<std::string> {
            auto r = txn.exec_params(
                "WITH w AS (SELECT date_trunc('week', (now() AT TIME ZONE $2))::date AS s) "
                "INSERT INTO ani_deals (owner_id, text, kind, habit_id, target, week_start) SELECT $1::uuid, $3, $4, "
                "$6::uuid, $5, w.s FROM w WHERE (SELECT count(*) FROM ani_deals WHERE owner_id = $1::uuid AND "
                "week_start = w.s) < $7 AND ($6::uuid IS NULL OR EXISTS (SELECT 1 FROM habits WHERE id = $6::uuid "
                "AND owner_id = $1::uuid)) RETURNING id::text",
                owner,
                tz,
                text,
                kind,
                target,
                habit_id,
                kMaxPerWeek);
            if (r.empty()) {
                return std::nullopt;
            }
            return r[0][0].template as<std::string>();
        });
    }

    /// The person's habit whose name is @p name (case-insensitive), not archived.
    std::optional<std::string> habit_named(const std::string& owner, const std::string& name) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<std::string> {
            auto r = txn.exec_params(
                "SELECT id::text FROM habits WHERE owner_id = $1::uuid AND archived_at IS NULL AND lower(name) = "
                "lower($2) LIMIT 1",
                owner,
                name);
            if (r.empty()) {
                return std::nullopt;
            }
            return r[0][0].template as<std::string>();
        });
    }

    bool remove(const std::string& owner, const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            return txn.exec_params("DELETE FROM ani_deals WHERE owner_id = $1::uuid AND id::text = $2", owner, id)
                       .affected_rows() > 0;
        });
    }

private:
    /// What a deal has so far in its week (d, w and $2, the zone, in scope).
    static std::string done_sql() {
        return "CASE d.kind "
               "WHEN 'workouts' THEN (SELECT count(*) FROM workout_sessions s WHERE s.owner_id = d.owner_id AND "
               "s.finished_at IS NOT NULL AND (s.started_at AT TIME ZONE $2)::date BETWEEN d.week_start AND "
               "d.week_start + 6) + (SELECT count(*) FROM workouts b JOIN mi_accounts m ON m.xiaomi_user_id = "
               "b.user_id WHERE m.owner_id = d.owner_id AND (b.start_at AT TIME ZONE $2)::date BETWEEN d.week_start "
               "AND d.week_start + 6 AND NOT EXISTS (SELECT 1 FROM workout_sessions s WHERE s.owner_id = d.owner_id "
               "AND s.band_workout_id = b.workout_id)) "
               "WHEN 'focus_minutes' THEN (SELECT COALESCE(sum(f.minutes), 0) FROM focus_sessions f WHERE "
               "f.owner_id = d.owner_id AND (f.started_at AT TIME ZONE $2)::date BETWEEN d.week_start AND "
               "d.week_start + 6) "
               "ELSE (SELECT count(*) FROM habit_checks c WHERE c.habit_id = d.habit_id AND c.date BETWEEN "
               "d.week_start AND d.week_start + 6) END";
    }
};

}  // namespace Repositories::Agent
