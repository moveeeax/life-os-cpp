/**
 * @file PrefsRepository.hpp
 * @brief ani_prefs (migration 062): Ani's settings kept on the server. The
 *        bare outfit is on while bare_on_at is set (the user said they are an
 *        adult when they turned it on). hold_until (migration 064): nothing
 *        is handed to the page before then.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories::Agent {

class PrefsRepository {
public:
    /// {"bare": bool}; a user without a row has everything off.
    nlohmann::json get(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT bare_on_at IS NOT NULL FROM ani_prefs WHERE owner_id = $1::uuid", owner);
            return nlohmann::json{{"bare", !r.empty() && r[0][0].template as<bool>()}};
        });
    }

    void set_bare(const std::string& owner, bool on) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO ani_prefs (owner_id, bare_on_at) VALUES ($1::uuid, CASE WHEN $2 THEN now() END) "
                "ON CONFLICT (owner_id) DO UPDATE SET bare_on_at = CASE WHEN $2 THEN COALESCE(ani_prefs.bare_on_at, "
                "now()) END",
                owner,
                on);
            return true;
        });
    }

    /// Holds everything back from the page for @p minutes; 0 ends a hold.
    void hold(const std::string& owner, int minutes) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO ani_prefs (owner_id, hold_until) VALUES ($1::uuid, CASE WHEN $2 > 0 THEN now() + "
                "make_interval(mins => $2) END) ON CONFLICT (owner_id) DO UPDATE SET hold_until = "
                "EXCLUDED.hold_until",
                owner,
                minutes);
            return true;
        });
    }

    /// When a hold in force ends (UTC, ISO-8601); nullopt when none.
    std::optional<std::string> held_until(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<std::string> {
            auto r = txn.exec_params(
                "SELECT to_char(hold_until AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') FROM ani_prefs "
                "WHERE owner_id = $1::uuid AND hold_until > now()",
                owner);
            if (r.empty() || r[0][0].is_null()) {
                return std::nullopt;
            }
            return r[0][0].template as<std::string>();
        });
    }
};

}  // namespace Repositories::Agent
