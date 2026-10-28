/**
 * @file ReminderRepository.hpp
 * @brief ani_reminders (migration 065): Ani's recurring reminders. The next
 *        time is computed by Postgres in the reminder's own zone (DST
 *        included): the first of the next eight local days on the mask's
 *        weekdays (Mon=1 .. Sun=64) whose at_time is after a moment.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories::Agent {

class ReminderRepository {
public:
    static constexpr int kMaxPerUser = 20;

    struct Due {
        std::string id;
        std::string owner;
        std::string text;
    };

    /// SQL for the next time after @p after of a reminder with mask @p days,
    /// local time @p at and zone @p tz (each an SQL expression).
    static std::string next_sql(const std::string& days,
                                const std::string& at,
                                const std::string& tz,
                                const std::string& after) {
        return "(SELECT min(t) FROM (SELECT ((((" + after + ") AT TIME ZONE " + tz + ")::date + d) + (" + at +
               ")::time) AT TIME ZONE " + tz + " AS t, ((" + after + ") AT TIME ZONE " + tz +
               ")::date + d AS day FROM generate_series(0, 7) d) x WHERE t > (" + after + ") AND ((" + days +
               ")::int & (1 << (extract(isodow FROM day)::int - 1))) <> 0)";
    }

    /// The user's reminders, oldest first.
    nlohmann::json list(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + columns() +
                                         " FROM ani_reminders WHERE owner_id = $1::uuid ORDER BY created_at, id) t",
                                     owner);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// Adds a reminder (already checked: text, HH:MM, mask 1..127, a known
    /// zone); nullopt when the user has kMaxPerUser already.
    std::optional<nlohmann::json> add(
        const std::string& owner, const std::string& text, const std::string& at, int days, const std::string& tz) {
        return Database::get().execute_write([&](auto& txn) -> std::optional<nlohmann::json> {
            txn.exec_params("SELECT pg_advisory_xact_lock(hashtext('ani_reminders:' || $1))", owner);
            const int n = txn.exec_params("SELECT count(*) FROM ani_reminders WHERE owner_id = $1::uuid", owner)[0][0]
                              .template as<int>();
            if (n >= kMaxPerUser) {
                return std::nullopt;
            }
            auto r = txn.exec_params(
                "INSERT INTO ani_reminders (owner_id, text, at_time, days, tz, next_at) VALUES ($1::uuid, $2, "
                "$3::time, $4, $5, " +
                    next_sql("$4", "$3", "$5", "now()") + ") RETURNING row_to_json((SELECT t FROM (SELECT " +
                    columns_of("ani_reminders") + ") t))::text",
                owner,
                text,
                at,
                days,
                tz);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    bool remove(const std::string& owner, const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            return !txn.exec_params(
                           "DELETE FROM ani_reminders WHERE owner_id = $1::uuid AND id::text = $2 "
                           "RETURNING id",
                           owner,
                           id)
                        .empty();
        });
    }

    /// Reminders due now, each moved on to its next time after now in the
    /// same statement (so a long pause yields one item, and two workers never
    /// take the same one).
    std::vector<Due> take_due(int limit = 200) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "UPDATE ani_reminders r SET next_at = " + next_sql("r.days", "r.at_time", "r.tz", "now()") +
                    " WHERE r.id IN (SELECT id FROM ani_reminders WHERE next_at <= now() ORDER BY next_at LIMIT $1 "
                    "FOR UPDATE SKIP LOCKED) RETURNING r.id::text, r.owner_id::text, r.text",
                limit);
            std::vector<Due> out;
            for (const auto& row : r) {
                out.push_back({row[0].template as<std::string>(),
                               row[1].template as<std::string>(),
                               row[2].template as<std::string>()});
            }
            return out;
        });
    }

private:
    static std::string columns() { return columns_of("ani_reminders"); }

    static std::string columns_of(const std::string& t) {
        return t + ".id::text AS id, " + t + ".text, to_char(" + t + ".at_time, 'HH24:MI') AS time, " + t + ".days, " +
               t + ".tz, to_char(" + t + ".next_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS next_at";
    }
};

}  // namespace Repositories::Agent
