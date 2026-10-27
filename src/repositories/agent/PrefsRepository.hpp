/**
 * @file PrefsRepository.hpp
 * @brief ani_prefs (migration 062): Ani's settings kept on the server. The
 *        bare outfit is on while bare_on_at is set (the user said they are an
 *        adult when they turned it on).
 */

#pragma once

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
};

}  // namespace Repositories::Agent
