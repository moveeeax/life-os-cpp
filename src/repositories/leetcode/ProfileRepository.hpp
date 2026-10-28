/**
 * @file ProfileRepository.hpp
 * @brief leetcode_profiles and leetcode_snapshots (migration 066): a user's
 *        LeetCode username and a snapshot of the solved counts per local day.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories::LeetCode {

class ProfileRepository {
public:
    struct Profile {
        std::string owner;
        std::string username;
        std::string tz;
    };

    /// {username, tz, synced_at, last_error, total, easy, medium, hard} (the
    /// counts of the latest snapshot, null before the first).
    std::optional<nlohmann::json> get(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT row_to_json(t)::text FROM (SELECT p.username, p.tz, to_char(p.synced_at AT TIME ZONE 'UTC', "
                "'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS synced_at, p.last_error, s.total, s.easy, s.medium, s.hard "
                "FROM leetcode_profiles p LEFT JOIN LATERAL (SELECT * FROM leetcode_snapshots x WHERE x.owner_id = "
                "p.owner_id ORDER BY x.day DESC LIMIT 1) s ON true WHERE p.owner_id = $1::uuid) t",
                owner);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    bool exists(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            return !txn.exec_params("SELECT 1 FROM leetcode_profiles WHERE owner_id = $1::uuid", owner).empty();
        });
    }

    /// Sets the username (already checked) and zone; a new username drops the
    /// old snapshots (their growth would be another person's).
    void set(const std::string& owner, const std::string& username, const std::string& tz) {
        Database::get().execute_write([&](auto& txn) {
            auto old = txn.exec_params("SELECT username FROM leetcode_profiles WHERE owner_id = $1::uuid", owner);
            if (!old.empty() && old[0][0].template as<std::string>() != username) {
                txn.exec_params("DELETE FROM leetcode_snapshots WHERE owner_id = $1::uuid", owner);
            }
            txn.exec_params(
                "INSERT INTO leetcode_profiles (owner_id, username, tz) VALUES ($1::uuid, $2, $3) ON CONFLICT "
                "(owner_id) DO UPDATE SET username = EXCLUDED.username, tz = EXCLUDED.tz, last_error = NULL",
                owner,
                username,
                tz);
            return true;
        });
    }

    bool remove(const std::string& owner) {
        return Database::get().execute_write([&](auto& txn) {
            txn.exec_params("DELETE FROM leetcode_snapshots WHERE owner_id = $1::uuid", owner);
            return !txn.exec_params("DELETE FROM leetcode_profiles WHERE owner_id = $1::uuid RETURNING owner_id", owner)
                        .empty();
        });
    }

    std::vector<Profile> all() {
        return Database::get().execute_read([&](auto& txn) {
            std::vector<Profile> out;
            for (const auto& row :
                 txn.exec("SELECT owner_id::text, username, tz FROM leetcode_profiles ORDER BY created_at")) {
                out.push_back({row[0].template as<std::string>(),
                               row[1].template as<std::string>(),
                               row[2].template as<std::string>()});
            }
            return out;
        });
    }

    /// Today's snapshot in the profile's zone (the latest pull of the day wins).
    void snapshot(const std::string& owner, const std::string& tz, int total, int easy, int medium, int hard) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO leetcode_snapshots (owner_id, day, total, easy, medium, hard) VALUES ($1::uuid, "
                "(now() AT TIME ZONE $2)::date, $3, $4, $5, $6) ON CONFLICT (owner_id, day) DO UPDATE SET total = "
                "EXCLUDED.total, easy = EXCLUDED.easy, medium = EXCLUDED.medium, hard = EXCLUDED.hard, taken_at = "
                "now()",
                owner,
                tz,
                total,
                easy,
                medium,
                hard);
            txn.exec_params(
                "UPDATE leetcode_profiles SET synced_at = now(), last_error = NULL WHERE owner_id = $1::uuid", owner);
            return true;
        });
    }

    void failed(const std::string& owner, const std::string& why) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE leetcode_profiles SET last_error = left($2, 200) WHERE owner_id = $1::uuid", owner, why);
            return true;
        });
    }
};

}  // namespace Repositories::LeetCode
