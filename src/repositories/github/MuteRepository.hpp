/**
 * @file MuteRepository.hpp
 * @brief github_mutes (migration 079): "Not about this", one attention rule
 *        silenced for one repository (by full name) of a user. The caller
 *        checks the rule with Github::is_rule and recomputes the verdict
 *        (RepoRepository::facts + Github::attention + set_attention).
 */

#pragma once

#include <map>
#include <set>
#include <string>

#include "database/Database.hpp"

namespace Repositories::Github {

class MuteRepository {
public:
    /// Mutes @p rule of the repository; true when it was not muted before.
    bool put(const std::string& owner, const std::string& full_name, const std::string& rule) {
        return Database::get().execute_write([&](auto& txn) {
            return txn.exec_params(
                          "INSERT INTO github_mutes (owner_id, full_name, rule) VALUES ($1::uuid, $2, $3) ON CONFLICT "
                          "DO NOTHING",
                          owner,
                          full_name,
                          rule)
                       .affected_rows() > 0;
        });
    }

    /// Unmutes; true when it was muted.
    bool remove(const std::string& owner, const std::string& full_name, const std::string& rule) {
        return Database::get().execute_write([&](auto& txn) {
            return txn.exec_params(
                          "DELETE FROM github_mutes WHERE owner_id = $1::uuid AND full_name = $2 AND rule = $3",
                          owner,
                          full_name,
                          rule)
                       .affected_rows() > 0;
        });
    }

    /// The muted rules of one repository.
    std::set<std::string> of(const std::string& owner, const std::string& full_name) {
        return Database::get().execute_read([&](auto& txn) {
            std::set<std::string> out;
            for (const auto& row : txn.exec_params(
                     "SELECT rule FROM github_mutes WHERE owner_id = $1::uuid AND full_name = $2", owner, full_name)) {
                out.insert(row[0].template as<std::string>());
            }
            return out;
        });
    }

    /// Every mute of the user, by repository full name (the sync reads them once).
    std::map<std::string, std::set<std::string>> all(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            std::map<std::string, std::set<std::string>> out;
            for (const auto& row :
                 txn.exec_params("SELECT full_name, rule FROM github_mutes WHERE owner_id = $1::uuid", owner)) {
                out[row[0].template as<std::string>()].insert(row[1].template as<std::string>());
            }
            return out;
        });
    }
};

}  // namespace Repositories::Github
