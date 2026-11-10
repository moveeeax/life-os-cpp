/**
 * @file CueRepository.hpp
 * @brief ani_cue_dismissals and ani_cues (migration 082): the cues the person
 *        sent away for a local day, and the cue her agent wrote itself (one
 *        per day and screen). Rows older than a week are dropped as new ones
 *        come; a cue lives one day.
 */

#pragma once

#include <optional>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories::Agent {

class CueRepository {
public:
    /// How many days of rows are kept.
    static constexpr int kKeepDays = 7;

    /// The clock of a local day in a zone.
    struct Clock {
        std::string today;       ///< Today in the zone, YYYY-MM-DD.
        int minute = 0;          ///< Minutes since local midnight now.
        long long now = 0;       ///< Unix seconds now.
        long long from = 0;      ///< The day's local midnight, unix seconds.
        long long to = 0;        ///< The next local midnight, unix seconds.
        std::string expires_at;  ///< The next local midnight, RFC 3339 in UTC.
    };

    /// @p date in @p tz (a zone PostgreSQL knows).
    Clock clock(const std::string& date, const std::string& tz) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT (now() AT TIME ZONE $2)::date::text, "
                " (extract(hour FROM now() AT TIME ZONE $2) * 60 + extract(minute FROM now() AT TIME ZONE $2))::int, "
                " extract(epoch FROM now())::bigint, "
                " extract(epoch FROM ($1::date::timestamp AT TIME ZONE $2))::bigint, "
                " extract(epoch FROM (($1::date + 1)::timestamp AT TIME ZONE $2))::bigint, "
                " to_char((($1::date + 1)::timestamp AT TIME ZONE $2) AT TIME ZONE 'UTC', "
                "  'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"')",
                date,
                tz);
            Clock c;
            c.today = r[0][0].template as<std::string>();
            c.minute = r[0][1].template as<int>();
            c.now = r[0][2].template as<long long>();
            c.from = r[0][3].template as<long long>();
            c.to = r[0][4].template as<long long>();
            c.expires_at = r[0][5].template as<std::string>();
            return c;
        });
    }

    /// The ids sent away on @p date.
    std::set<std::string> dismissed(const std::string& owner, const std::string& date) {
        return Database::get().execute_read([&](auto& txn) {
            std::set<std::string> out;
            for (const auto& row :
                 txn.exec_params("SELECT cue_id FROM ani_cue_dismissals WHERE owner_id = $1::uuid AND day = $2::date",
                                 owner,
                                 date)) {
                out.insert(row[0].template as<std::string>());
            }
            return out;
        });
    }

    /// Sends @p cue_id away for @p date; again is a no-op.
    void dismiss(const std::string& owner, const std::string& date, const std::string& cue_id) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO ani_cue_dismissals (owner_id, day, cue_id) VALUES ($1::uuid, $2::date, $3) "
                "ON CONFLICT DO NOTHING",
                owner,
                date,
                cue_id);
            txn.exec_params("DELETE FROM ani_cue_dismissals WHERE owner_id = $1::uuid AND day < $2::date - $3::int",
                            owner,
                            date,
                            kKeepDays);
            return true;
        });
    }

    /// The agent's cues of @p date: [{id, screen, text, action}], in the order of the screens.
    nlohmann::json of_day(const std::string& owner, const std::string& date) {
        return Database::get().execute_read([&](auto& txn) {
            return nlohmann::json::parse(
                txn.exec_params(
                       "SELECT COALESCE(json_agg(json_build_object('id', id::text, 'screen', screen, 'text', text, "
                       "'action', action) ORDER BY array_position(ARRAY['today', 'food', 'body', 'money'], screen)), "
                       "'[]'::json)::text FROM ani_cues WHERE owner_id = $1::uuid AND day = $2::date",
                       owner,
                       date)[0][0]
                    .template as<std::string>());
        });
    }

    /// Whether @p id is an agent cue of @p owner on @p date.
    bool exists(const std::string& owner, const std::string& date, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) {
            return !txn.exec_params(
                           "SELECT 1 FROM ani_cues WHERE owner_id = $1::uuid AND day = $2::date AND id::text = $3",
                           owner,
                           date,
                           id)
                        .empty();
        });
    }

    /// The agent's cue on @p screen for today in @p tz, replacing the one there (a new id); its id.
    std::string put(const std::string& owner,
                    const std::string& tz,
                    const std::string& screen,
                    const std::string& text,
                    const nlohmann::json& action) {
        const std::optional<std::string> action_text =
            action.is_null() ? std::optional<std::string>() : std::optional<std::string>(action.dump());
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "INSERT INTO ani_cues (owner_id, day, screen, text, action) "
                "VALUES ($1::uuid, (now() AT TIME ZONE $2)::date, $3, $4, $5::jsonb) "
                "ON CONFLICT (owner_id, day, screen) DO UPDATE SET id = gen_random_uuid(), text = EXCLUDED.text, "
                "action = EXCLUDED.action, created_at = now() RETURNING id::text, day::text",
                owner,
                tz,
                screen,
                text,
                action_text);
            const std::string day = r[0][1].template as<std::string>();
            txn.exec_params(
                "DELETE FROM ani_cues WHERE owner_id = $1::uuid AND day < $2::date - $3::int", owner, day, kKeepDays);
            return r[0][0].template as<std::string>();
        });
    }

    /// Drops the agent's cue on @p screen for today in @p tz; whether there was one.
    bool clear(const std::string& owner, const std::string& tz, const std::string& screen) {
        return Database::get().execute_write([&](auto& txn) {
            return txn.exec_params(
                          "DELETE FROM ani_cues WHERE owner_id = $1::uuid AND day = (now() AT TIME ZONE $2)::date AND "
                          "screen = $3",
                          owner,
                          tz,
                          screen)
                       .affected_rows() > 0;
        });
    }
};

}  // namespace Repositories::Agent
