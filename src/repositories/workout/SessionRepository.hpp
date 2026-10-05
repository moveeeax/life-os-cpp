/**
 * @file SessionRepository.hpp
 * @brief Logged workouts of the workout module: sessions, their exercises and
 *        sets, and the reconciliation that attaches Mi Fitness data to them.
 *
 * Rows leave as JSON built by Postgres. A session is always read and written
 * through its owner; the Mi Fitness tables are global to the installation
 * (one Xiaomi account) and are joined by time alone.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/RepoErrors.hpp"
#include "repositories/SqlErrors.hpp"
#include "repositories/workout/ExerciseRepository.hpp"
#include "repositories/workout/RoutineRepository.hpp"

namespace Repositories {

struct SessionNotFound : NotFoundError {
    SessionNotFound() : NotFoundError("workout_session") {}
};

struct SessionExerciseNotFound : NotFoundError {
    SessionExerciseNotFound() : NotFoundError("session_exercise") {}
};

struct SetNotFound : NotFoundError {
    SetNotFound() : NotFoundError("workout_set") {}
};

struct SessionActive : ConflictError {
    explicit SessionActive(const std::string& id)
        : ConflictError("session_active", "a workout session is already active: " + id) {}
};

struct InvalidSessionTimes : ValidationError {
    InvalidSessionTimes() : ValidationError("invalid_times", "finished_at must not be before started_at") {}
};

struct InvalidTimestamp : ValidationError {
    InvalidTimestamp() : ValidationError("invalid_timestamp", "not a valid date and time") {}
};

class SessionRepository {
public:
    struct Page {
        nlohmann::json rows = nlohmann::json::array();
        long total = 0;
    };

    /// A partial update of a session: nullopt leaves the column as it is.
    struct Patch {
        std::optional<std::string> name;
        std::optional<std::string> note;
        std::optional<std::string> started_at;
        std::optional<std::string> finished_at;
        /// Finish now when the session has no finished_at yet.
        bool finish = false;

        bool touches_times() const { return started_at || finished_at || finish; }
    };

    /// One set as the client sends it: nullopt stores NULL.
    struct SetInput {
        std::string session_exercise_id;
        int position = 1;
        std::string kind = "work";
        std::optional<double> weight_kg;
        std::optional<int> reps;
        std::optional<int> duration_seconds;
        std::optional<double> distance_m;
        std::optional<double> rpe;
        std::optional<std::string> completed_at;
    };

    /**
     * @brief Start a session, empty or from a routine: the routine's exercises
     *        and targets are copied, and the latest body weight is stored.
     * @throws SessionActive when the owner already has an unfinished session.
     * @throws RoutineNotFound when the routine is not the owner's.
     */
    nlohmann::json start(const std::string& owner, const std::optional<std::string>& routine_id) {
        return Database::get().execute_write([&](auto& txn) {
            auto active = txn.exec_params(
                "SELECT id::text FROM workout_sessions WHERE owner_id = $1::uuid "
                "AND finished_at IS NULL",
                owner);
            if (!active.empty()) {
                throw SessionActive(active[0][0].template as<std::string>());
            }
            if (routine_id) {
                auto routine = txn.exec_params(
                    "SELECT 1 FROM routines WHERE id = $2::uuid AND owner_id = $1::uuid", owner, *routine_id);
                if (routine.empty()) {
                    throw RoutineNotFound();
                }
            }
            auto r = txn.exec_params(
                "WITH s AS ("
                " INSERT INTO workout_sessions (owner_id, routine_id, name, bodyweight_kg) "
                " VALUES ($1::uuid, $2::uuid, "
                "  COALESCE((SELECT name FROM routines WHERE id = $2::uuid AND owner_id = $1::uuid), ''), "
                "  (SELECT weight_kg FROM body_measurements "
                "    WHERE user_id = (SELECT xiaomi_user_id FROM mi_accounts WHERE owner_id = $1::uuid) "
                "    ORDER BY timestamp DESC LIMIT 1)) "
                " RETURNING id), "
                "x AS ("
                " INSERT INTO session_exercises (session_id, exercise_id, position, target_sets, target_reps_min, "
                "  target_reps_max, target_duration_seconds, rest_seconds, note) "
                " SELECT s.id, re.exercise_id, re.position, re.target_sets, re.target_reps_min, re.target_reps_max, "
                "  re.target_duration_seconds, re.rest_seconds, re.note "
                " FROM s JOIN routine_exercises re ON re.routine_id = $2::uuid) "
                "SELECT id::text FROM s",
                owner,
                routine_id);
            return *find_in(txn, owner, r[0][0].template as<std::string>());
        });
    }

    /// One session in full, or nullopt.
    std::optional<nlohmann::json> find(const std::string& owner, const std::string& id) {
        return Database::get().execute_read(
            [&](auto& txn) -> std::optional<nlohmann::json> { return find_in(txn, owner, id); });
    }

    /// The owner's unfinished session in full, or nullopt.
    std::optional<nlohmann::json> active(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT id::text FROM workout_sessions WHERE owner_id = $1::uuid "
                "AND finished_at IS NULL",
                owner);
            if (r.empty()) {
                return std::nullopt;
            }
            return find_in(txn, owner, r[0][0].template as<std::string>());
        });
    }

    /// Finished sessions, newest first, with counts and volume.
    Page list(const std::string& owner, long limit, long offset) {
        Page out;
        Database::get().execute_read([&](auto& txn) {
            auto agg = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM ("
                " SELECT s.id, s.name, s.routine_id, " +
                    iso("s.started_at") + " AS started_at, " + iso("s.finished_at") +
                    " AS finished_at, "
                    "  s.health_status, s.hr_avg, s.hr_max, s.band_calories_kcal, "
                    "  (SELECT COUNT(*) FROM session_exercises se WHERE se.session_id = s.id) AS exercise_count, "
                    "  (SELECT COUNT(*) FROM workout_sets ws JOIN session_exercises se "
                    "    ON se.id = ws.session_exercise_id "
                    "   WHERE se.session_id = s.id AND ws.kind = 'work') AS set_count, "
                    // Volume: reps x load over work sets; a bodyweight exercise adds the
                    // session's body weight to the set's extra load.
                    "  (SELECT COALESCE(SUM(ws.reps * (COALESCE(ws.weight_kg, 0) + "
                    "     CASE WHEN e.tracking_mode = 'bodyweight_reps' THEN COALESCE(s.bodyweight_kg, 0) "
                    "          ELSE 0 END)), 0) "
                    "   FROM workout_sets ws JOIN session_exercises se ON se.id = ws.session_exercise_id "
                    "   JOIN exercises e ON e.id = se.exercise_id "
                    "   WHERE se.session_id = s.id AND ws.kind = 'work' AND ws.reps IS NOT NULL) AS volume_kg "
                    " FROM workout_sessions s WHERE s.owner_id = $1::uuid AND s.finished_at IS NOT NULL "
                    " ORDER BY s.started_at DESC LIMIT $2 OFFSET $3) t",
                owner,
                limit,
                offset);
            out.rows = nlohmann::json::parse(agg[0][0].template as<std::string>());
            auto total = txn.exec_params(
                "SELECT COUNT(*) FROM workout_sessions WHERE owner_id = $1::uuid "
                "AND finished_at IS NOT NULL",
                owner);
            out.total = total[0][0].template as<long>();
            return 0;
        });
        return out;
    }

    /**
     * @brief Rename, annotate, finish or move a session. A change of its times
     *        clears the reconciliation result and reconciles again.
     * @throws SessionNotFound, InvalidSessionTimes.
     */
    nlohmann::json patch(const std::string& owner, const std::string& id, const Patch& p) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE workout_sessions SET "
                        " name = COALESCE($3, name), note = COALESCE($4, note), "
                        " started_at = COALESCE($5::timestamptz, started_at), "
                        " finished_at = COALESCE($6::timestamptz, "
                        "   CASE WHEN $7::boolean AND finished_at IS NULL THEN now() ELSE finished_at END) "
                        "WHERE id = $2::uuid AND owner_id = $1::uuid RETURNING (finished_at IS NOT NULL)",
                        owner,
                        id,
                        p.name,
                        p.note,
                        p.started_at,
                        p.finished_at,
                        p.finish);
                    if (r.empty()) {
                        throw SessionNotFound();
                    }
                    if (p.touches_times() && r[0][0].template as<bool>()) {
                        reconcile_in(txn, id);
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &translate);
    }

    /// @throws SessionNotFound.
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM workout_sessions WHERE id = $2::uuid AND owner_id = $1::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw SessionNotFound();
            }
            return 0;
        });
    }

    /**
     * @brief Append an exercise to a session.
     * @throws SessionNotFound, ExerciseNotFound.
     */
    nlohmann::json add_exercise(const std::string& owner, const std::string& id, const std::string& exercise_id) {
        return Database::get().execute_write([&](auto& txn) {
            auto session = txn.exec_params(
                "SELECT 1 FROM workout_sessions WHERE id = $2::uuid AND owner_id = $1::uuid", owner, id);
            if (session.empty()) {
                throw SessionNotFound();
            }
            auto r = txn.exec_params(
                "INSERT INTO session_exercises (session_id, exercise_id, position) "
                "SELECT $2::uuid, e.id, "
                " COALESCE((SELECT MAX(position) FROM session_exercises WHERE session_id = $2::uuid), 0) + 1 "
                "FROM exercises e WHERE e.id = $3 AND (e.source = 'library' OR e.owner_id = $1::uuid) "
                "RETURNING id",
                owner,
                id,
                exercise_id);
            if (r.empty()) {
                throw ExerciseNotFound();
            }
            return *find_in(txn, owner, id);
        });
    }

    /// Drop an exercise and its sets from a session. @throws SessionExerciseNotFound.
    nlohmann::json remove_exercise(const std::string& owner,
                                   const std::string& id,
                                   const std::string& session_exercise_id) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM session_exercises se USING workout_sessions s "
                "WHERE se.id = $3::uuid AND se.session_id = $2::uuid AND s.id = se.session_id "
                "AND s.owner_id = $1::uuid RETURNING se.id",
                owner,
                id,
                session_exercise_id);
            if (r.empty()) {
                throw SessionExerciseNotFound();
            }
            return *find_in(txn, owner, id);
        });
    }

    /**
     * @brief Create the set with this client-generated id, or replace it.
     *        Sending the same set twice is a replace, never a duplicate.
     * @throws SessionExerciseNotFound when the session exercise is not the
     *         owner's, or the id already names a set elsewhere.
     */
    nlohmann::json put_set(const std::string& owner, const std::string& set_id, const SetInput& s) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO workout_sets (id, session_exercise_id, position, kind, weight_kg, reps, "
                        " duration_seconds, distance_m, rpe, completed_at) "
                        "SELECT $1::uuid, se.id, $4, $5, $6, $7, $8, $9, $10, COALESCE($11::timestamptz, now()) "
                        "FROM session_exercises se JOIN workout_sessions ws ON ws.id = se.session_id "
                        "WHERE se.id = $3::uuid AND ws.owner_id = $2::uuid "
                        "ON CONFLICT (id) DO UPDATE SET position = EXCLUDED.position, kind = EXCLUDED.kind, "
                        " weight_kg = EXCLUDED.weight_kg, reps = EXCLUDED.reps, "
                        " duration_seconds = EXCLUDED.duration_seconds, distance_m = EXCLUDED.distance_m, "
                        " rpe = EXCLUDED.rpe, completed_at = EXCLUDED.completed_at "
                        " WHERE workout_sets.session_exercise_id = EXCLUDED.session_exercise_id "
                        "RETURNING json_build_object('id', id, 'session_exercise_id', session_exercise_id, "
                        " 'position', position, 'kind', kind, 'weight_kg', weight_kg, 'reps', reps, "
                        " 'duration_seconds', duration_seconds, 'distance_m', distance_m, 'rpe', rpe, "
                        " 'completed_at', " +
                            iso("completed_at") + ")",
                        set_id,
                        owner,
                        s.session_exercise_id,
                        s.position,
                        s.kind,
                        s.weight_kg,
                        s.reps,
                        s.duration_seconds,
                        s.distance_m,
                        s.rpe,
                        s.completed_at);
                    if (r.empty()) {
                        throw SessionExerciseNotFound();
                    }
                    return nlohmann::json::parse(r[0][0].template as<std::string>());
                });
            },
            &translate);
    }

    /// @throws SetNotFound.
    void remove_set(const std::string& owner, const std::string& set_id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM workout_sets w USING session_exercises se, workout_sessions s "
                "WHERE w.id = $2::uuid AND se.id = w.session_exercise_id AND s.id = se.session_id "
                "AND s.owner_id = $1::uuid RETURNING w.id",
                owner,
                set_id);
            if (r.empty()) {
                throw SetNotFound();
            }
            return 0;
        });
    }

    /**
     * @brief Heart-rate samples inside the session (until now for an active
     *        one), plus the time of the newest sample in the database, so the
     *        page can tell "no data yet" from "no data".
     * @throws SessionNotFound.
     */
    nlohmann::json heart_rate(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT json_build_object("
                " 'samples', COALESCE((SELECT json_agg(json_build_object('timestamp', " +
                    iso("h.timestamp") +
                    ", 'bpm', h.bpm) ORDER BY h.timestamp) FROM heart_rate_samples h "
                    "  WHERE h.user_id = acc.id AND h.timestamp >= s.started_at "
                    "   AND h.timestamp <= COALESCE(s.finished_at, now())), "
                    "  '[]'::json), "
                    " 'latest_sample_at', (SELECT " +
                    iso("MAX(timestamp)") +
                    " FROM heart_rate_samples WHERE user_id = acc.id)) "
                    "FROM workout_sessions s "
                    // The band data of the session's owner; no link, no samples.
                    "LEFT JOIN LATERAL (SELECT xiaomi_user_id AS id FROM mi_accounts "
                    "  WHERE owner_id = s.owner_id) acc ON true "
                    "WHERE s.id = $2::uuid AND s.owner_id = $1::uuid",
                owner,
                id);
            if (r.empty()) {
                throw SessionNotFound();
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /**
     * @brief Numbers to look at before a workout, each next to its 30-day
     *        mean: last night's sleep, resting heart rate, recent stress.
     *        A value without data is null.
     */
    nlohmann::json readiness(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            // Every number comes from the owner's Mi account; without a link
            // acc.id is NULL and every value is null.
            auto r = txn.exec_params(
                "SELECT json_build_object("
                // The longest non-nap sleep that ended in the last 18 hours.
                " 'sleep_minutes', (SELECT duration_minutes FROM sleep_sessions WHERE user_id = acc.id AND NOT is_nap "
                "   AND end_at > now() - interval '18 hours' ORDER BY duration_minutes DESC LIMIT 1), "
                " 'sleep_score', (SELECT sleep_score FROM sleep_sessions WHERE user_id = acc.id AND NOT is_nap "
                "   AND end_at > now() - interval '18 hours' ORDER BY duration_minutes DESC LIMIT 1), "
                // Mean over the last 30 days of each day's longest non-nap sleep.
                " 'sleep_minutes_avg_30d', (SELECT ROUND(AVG(d))::int FROM (SELECT MAX(duration_minutes) AS d "
                "   FROM sleep_sessions WHERE user_id = acc.id AND NOT is_nap "
                "    AND end_at > now() - interval '30 days' "
                "   GROUP BY (end_at AT TIME ZONE 'UTC')::date) q), "
                " 'resting_bpm', (SELECT bpm FROM heart_rate_samples WHERE user_id = acc.id "
                "   AND sample_type = 'resting' "
                "   AND timestamp > now() - interval '24 hours' ORDER BY timestamp DESC LIMIT 1), "
                " 'resting_bpm_avg_30d', (SELECT ROUND(AVG(bpm))::int FROM heart_rate_samples "
                "   WHERE user_id = acc.id AND sample_type = 'resting' AND timestamp > now() - interval '30 days'), "
                " 'stress', (SELECT ROUND(AVG(stress_score))::int FROM stress_samples "
                "   WHERE user_id = acc.id AND timestamp > now() - interval '12 hours'), "
                " 'stress_avg_30d', (SELECT ROUND(AVG(stress_score))::int FROM stress_samples "
                "   WHERE user_id = acc.id AND timestamp > now() - interval '30 days'), "
                " 'bodyweight_kg', (SELECT weight_kg FROM body_measurements WHERE user_id = acc.id "
                "   ORDER BY timestamp DESC LIMIT 1)) "
                "FROM (SELECT (SELECT xiaomi_user_id FROM mi_accounts WHERE owner_id = $1::uuid) AS id) acc",
                owner);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /**
     * @brief Attach Mi Fitness data to the owner's sessions that finished in
     *        the last @p window_days days. Idempotent; returns how many
     *        sessions were written.
     */
    long reconcile_recent(const std::string& owner, int window_days) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(std::string(kReconcileHead) +
                                         "s.owner_id = $1::uuid AND s.finished_at > now() - make_interval(days => $2)" +
                                         kReconcileTail,
                                     owner,
                                     window_days);
            return static_cast<long>(r.size());
        });
    }

    /// Reconcile the owner's sessions that finished on the given UTC dates (inclusive).
    /// @throws InvalidTimestamp when a date does not exist.
    long reconcile_range(const std::string& owner, const std::string& from, const std::string& to) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(std::string(kReconcileHead) +
                                                 "s.owner_id = $3::uuid AND s.finished_at >= $1::date "
                                                 "AND s.finished_at < $2::date + 1" +
                                                 kReconcileTail,
                                             from,
                                             to,
                                             owner);
                    return static_cast<long>(r.size());
                });
            },
            &translate);
    }

private:
    /// SQLSTATE -> typed error: a CHECK on the session times, a malformed or
    /// out-of-range timestamp. Anything else is rethrown by translate_sql.
    static void translate(std::string_view sqlstate) {
        if (sqlstate == "23514") {
            throw InvalidSessionTimes();
        }
        if (sqlstate == "22007" || sqlstate == "22008") {
            throw InvalidTimestamp();
        }
    }

    static std::string iso(const std::string& expr) {
        return "to_char(" + expr + " AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"')";
    }

    // Reconciliation, one statement. The band data is that of the session
    // owner's Mi account (cand.acct; NULL without a link, and then nothing
    // matches and the session stays pending). For each candidate session:
    // heart-rate samples inside it, and the band workout that overlaps it by more than
    // half of the session (the largest overlap when there are several). A band
    // workout's own heart rate wins over the sample-based one. Status:
    // matched with any data; otherwise pending while the newest sample in the
    // database is older than the session's end, no_data once newer ones exist.
    static constexpr const char* kReconcileHead =
        "WITH cand AS ("
        " SELECT s.id, s.started_at, s.finished_at, "
        "  (SELECT m.xiaomi_user_id FROM mi_accounts m WHERE m.owner_id = s.owner_id) AS acct "
        " FROM workout_sessions s "
        " WHERE s.finished_at IS NOT NULL AND ";
    static constexpr const char* kReconcileTail =
        "), "
        "hr AS ("
        " SELECT c.id, COUNT(h.bpm) AS n, ROUND(AVG(h.bpm))::int AS avg_bpm, MAX(h.bpm) AS max_bpm "
        " FROM cand c LEFT JOIN heart_rate_samples h "
        "  ON h.user_id = c.acct AND h.timestamp >= c.started_at AND h.timestamp <= c.finished_at "
        " GROUP BY c.id), "
        "band AS ("
        " SELECT DISTINCT ON (c.id) c.id, w.workout_id, w.calories_kcal, w.avg_heart_rate_bpm, "
        "  w.max_heart_rate_bpm "
        " FROM cand c JOIN workouts w "
        "  ON w.user_id = c.acct AND w.start_at < c.finished_at AND w.end_at > c.started_at "
        " WHERE EXTRACT(EPOCH FROM (LEAST(c.finished_at, w.end_at) - GREATEST(c.started_at, w.start_at))) * 2 "
        "       > EXTRACT(EPOCH FROM (c.finished_at - c.started_at)) "
        " ORDER BY c.id, LEAST(c.finished_at, w.end_at) - GREATEST(c.started_at, w.start_at) DESC, "
        "  w.workout_id) "
        "UPDATE workout_sessions s SET "
        " hr_samples = hr.n, "
        " hr_avg = COALESCE(band.avg_heart_rate_bpm, hr.avg_bpm), "
        " hr_max = COALESCE(band.max_heart_rate_bpm, hr.max_bpm), "
        " band_workout_id = band.workout_id, band_calories_kcal = band.calories_kcal, "
        " health_status = CASE WHEN hr.n > 0 OR band.workout_id IS NOT NULL THEN 'matched' "
        "   WHEN latest.ts IS NULL OR latest.ts < s.finished_at THEN 'pending' ELSE 'no_data' END, "
        " reconciled_at = now() "
        "FROM cand c JOIN hr ON hr.id = c.id LEFT JOIN band ON band.id = c.id "
        // The newest sample of the same account tells "not synced yet" from "no data".
        "LEFT JOIN LATERAL (SELECT MAX(timestamp) AS ts FROM heart_rate_samples "
        "  WHERE user_id = c.acct) latest ON true "
        "WHERE s.id = c.id RETURNING s.id";

    /// One session by id, whoever owns it: the caller has checked the owner.
    template <typename Txn>
    static long reconcile_in(Txn& txn, const std::string& id) {
        auto r = txn.exec_params(std::string(kReconcileHead) + "s.id = $1::uuid" + kReconcileTail, id);
        return static_cast<long>(r.size());
    }

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params(
            "SELECT row_to_json(t) FROM ("
            " SELECT s.id, s.routine_id, s.name, " +
                iso("s.started_at") + " AS started_at, " + iso("s.finished_at") +
                " AS finished_at, "
                "  s.note, s.bodyweight_kg, s.health_status, s.hr_avg, s.hr_max, s.hr_samples, "
                "  s.band_workout_id, s.band_calories_kcal, " +
                iso("s.reconciled_at") +
                " AS reconciled_at, "
                "  COALESCE((SELECT json_agg(x ORDER BY x.position) FROM ("
                "    SELECT se.id, se.exercise_id, se.position, se.target_sets, se.target_reps_min, "
                "     se.target_reps_max, se.target_duration_seconds, se.rest_seconds, se.note, "
                "     e.name AS exercise_name, e.tracking_mode, e.images, e.primary_muscles, "
                "     COALESCE((SELECT json_agg(y ORDER BY y.position) FROM ("
                "       SELECT ws.id, ws.position, ws.kind, ws.weight_kg, ws.reps, ws.duration_seconds, "
                "        ws.distance_m, ws.rpe, " +
                iso("ws.completed_at") +
                " AS completed_at "
                "       FROM workout_sets ws WHERE ws.session_exercise_id = se.id) y), '[]'::json) AS sets, "
                // The sets of the latest earlier finished session that logged this
                // exercise: what the page pre-fills weight and reps from.
                "     COALESCE((SELECT json_agg(p ORDER BY p.position) FROM ("
                "       SELECT ws.position, ws.kind, ws.weight_kg, ws.reps, ws.duration_seconds, "
                "        ws.distance_m, ws.rpe "
                "       FROM workout_sets ws WHERE ws.session_exercise_id = ("
                "        SELECT se2.id FROM session_exercises se2 "
                "        JOIN workout_sessions s2 ON s2.id = se2.session_id "
                "        WHERE se2.exercise_id = se.exercise_id AND s2.owner_id = s.owner_id "
                "         AND s2.finished_at IS NOT NULL AND s2.started_at < s.started_at "
                "         AND EXISTS (SELECT 1 FROM workout_sets w3 WHERE w3.session_exercise_id = se2.id) "
                "        ORDER BY s2.started_at DESC LIMIT 1)) p), '[]'::json) AS previous_sets "
                "    FROM session_exercises se JOIN exercises e ON e.id = se.exercise_id "
                "    WHERE se.session_id = s.id) x), '[]'::json) AS exercises "
                " FROM workout_sessions s WHERE s.id = $2::uuid AND s.owner_id = $1::uuid) t",
            owner,
            id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories
