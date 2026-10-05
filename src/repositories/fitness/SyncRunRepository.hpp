/**
 * @file SyncRunRepository.hpp
 * @brief Sync run journal (migration 012, table sync_runs).
 *
 * A run is created before the job is enqueued and closed with a per-type
 * result. Statuses: running, succeeded, failed, interrupted, skipped.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories {

/// Advisory lock key of the single concurrent sync. An arbitrary constant;
/// all that matters is that it is one per system.
inline constexpr long long kSyncAdvisoryLockKey = 0x4d69466974;

class SyncRunRepository {
public:
    /// A queued run of one Xiaomi account.
    long create(const std::string& xiaomi_user_id,
                const std::string& from,
                const std::string& to,
                const std::vector<std::string>& data_types) {
        // Types are internal identifiers without commas or braces, so the
        // array literal is built by concatenation without escaping.
        std::string array_literal = "{";
        for (const auto& type : data_types) {
            if (array_literal.size() > 1) {
                array_literal += ",";
            }
            array_literal += type;
        }
        array_literal += "}";
        return Database::get().execute_write([&](auto& txn) {
            // Status queued, not running: the running row is one per account by
            // the partial unique index, and the executor must claim it with an
            // atomic transition, not the enqueuer.
            auto r = txn.exec_params(
                "INSERT INTO sync_runs (status, requested_start, requested_end, data_types, xiaomi_user_id) "
                "VALUES ('queued', $1, $2, $3::text[], $4) RETURNING id",
                from,
                to,
                array_literal,
                xiaomi_user_id);
            return r[0][0].template as<long>();
        });
    }

    void finish(long id, const std::string& status, const nlohmann::json& result) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE sync_runs SET status = $2, result = $3::jsonb, finished_at = now() "
                "WHERE id = $1",
                id,
                status,
                result.dump());
            return true;
        });
    }

    /// Mark skipped only a run that is still queued. A redelivered job of a
    /// finished run must not overwrite its journal (Important 4 of the final
    /// review: the same hole from the skip side).
    void skip_if_queued(long id, const nlohmann::json& result) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE sync_runs SET status = 'skipped', result = $2::jsonb, finished_at = now() "
                "WHERE id = $1 AND status = 'queued'",
                id,
                result.dump());
            return true;
        });
    }

    /// Whether a run of this account is live: a probe or an unlink must not
    /// log in or delete rows under it.
    bool any_running(const std::string& xiaomi_user_id) {
        return Database::get().execute_read([&](auto& txn) {
            return !txn.exec_params("SELECT 1 FROM sync_runs WHERE status = 'running' AND xiaomi_user_id = $1 LIMIT 1",
                                    xiaomi_user_id)
                        .empty();
        });
    }

    /// A run of this account; a run of another account does not exist for the caller.
    std::optional<nlohmann::json> get(long id, const std::string& xiaomi_user_id) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT id, started_at::text, finished_at::text, status, "
                "requested_start::text, requested_end::text, "
                "array_to_json(data_types)::text, result::text "
                "FROM sync_runs WHERE id = $1 AND xiaomi_user_id = $2",
                id,
                xiaomi_user_id);
            if (r.empty()) {
                return std::nullopt;
            }
            const auto& row = r[0];
            nlohmann::json out;
            out["id"] = row[0].template as<long>();
            out["started_at"] = row[1].template as<std::string>();
            out["finished_at"] =
                row[2].is_null() ? nlohmann::json() : nlohmann::json(row[2].template as<std::string>());
            out["status"] = row[3].template as<std::string>();
            out["requested_start"] =
                row[4].is_null() ? nlohmann::json() : nlohmann::json(row[4].template as<std::string>());
            out["requested_end"] =
                row[5].is_null() ? nlohmann::json() : nlohmann::json(row[5].template as<std::string>());
            out["data_types"] = nlohmann::json::parse(row[6].template as<std::string>());
            out["result"] = nlohmann::json::parse(row[7].template as<std::string>());
            return out;
        });
    }
};

}  // namespace Repositories
