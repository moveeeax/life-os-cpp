/**
 * @file DayRepository.hpp
 * @brief What the day ribbon on Now draws from Life OS itself (migration 069):
 *        focus sessions, workouts and tasks with a time on that day. The
 *        calendar's events come from Google (calendar/Access.hpp).
 */

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories::Now {

class DayRepository {
public:
    /// Records a finished session once per start (a second tab gets the same id); @p minutes is the time in
    /// focus (pauses left out).
    std::string add_focus(const std::string& owner,
                          const std::string& started_at,
                          const std::string& ended_at,
                          int minutes,
                          const std::string& label) {
        return Database::get().execute_write([&](auto& txn) {
            return txn
                .exec_params(
                    "INSERT INTO focus_sessions (owner_id, started_at, ended_at, minutes, label) VALUES ($1::uuid, "
                    "$2::timestamptz, $3::timestamptz, $4, $5) ON CONFLICT (owner_id, started_at) DO UPDATE SET "
                    "started_at = "
                    "EXCLUDED.started_at RETURNING id::text",
                    owner,
                    started_at,
                    ended_at,
                    minutes,
                    label)[0][0]
                .template as<std::string>();
        });
    }

    /// {focus, workouts, tasks} of the local @p date in @p tz.
    nlohmann::json day(const std::string& owner, const std::string& date, const std::string& tz) {
        return Database::get().execute_read([&](auto& txn) {
            // The local day as two instants: its midnight and the next.
            const char* kLo = "(($2::date)::timestamp AT TIME ZONE $3)";
            const char* kHi = "((($2::date) + 1)::timestamp AT TIME ZONE $3)";
            const char* kIso = "'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'";
            auto focus = txn.exec_params(
                std::string("SELECT COALESCE(json_agg(json_build_object('start', to_char(started_at AT TIME ZONE "
                            "'UTC', ") +
                    kIso + "), 'end', to_char(ended_at AT TIME ZONE 'UTC', " + kIso +
                    "), 'minutes', minutes, 'label', label) ORDER BY started_at), '[]'::json)::text FROM "
                    "focus_sessions WHERE owner_id = $1::uuid AND ended_at > " +
                    kLo + " AND started_at < " + kHi,
                owner,
                date,
                tz);
            auto workouts = txn.exec_params(
                std::string("SELECT COALESCE(json_agg(json_build_object('start', to_char(started_at AT TIME ZONE "
                            "'UTC', ") +
                    kIso +
                    "), 'end', CASE WHEN finished_at IS NULL THEN NULL ELSE to_char(finished_at AT TIME ZONE "
                    "'UTC', " +
                    kIso +
                    ") END, 'name', name) ORDER BY started_at), '[]'::json)::text FROM workout_sessions WHERE "
                    "owner_id = $1::uuid AND COALESCE(finished_at, now()) > " +
                    kLo + " AND started_at < " + kHi,
                owner,
                date,
                tz);
            auto tasks = txn.exec_params(
                "SELECT COALESCE(json_agg(json_build_object('id', id::text, 'title', title, 'time', "
                "to_char(due_time, 'HH24:MI'), 'status', status) ORDER BY due_time), '[]'::json)::text FROM "
                "task_items WHERE owner_id = $1::uuid AND due = $2::date AND due_time IS NOT NULL",
                owner,
                date);
            return nlohmann::json{{"focus", nlohmann::json::parse(focus[0][0].template as<std::string>())},
                                  {"workouts", nlohmann::json::parse(workouts[0][0].template as<std::string>())},
                                  {"tasks", nlohmann::json::parse(tasks[0][0].template as<std::string>())}};
        });
    }
};

}  // namespace Repositories::Now
