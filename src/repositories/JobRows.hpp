/**
 * @file JobRows.hpp
 * @brief The life cycle the LLM job tables share (food, money and tasks parse
 *        jobs, money advisor reports): the claim, the requeue, what counts as
 *        open, and the status a worker reads back.
 *
 * A row goes queued -> running (one more attempt) -> done | failed, or back to
 * queued for an outage the queue retries. A `running` row older than
 * kStaleRunning was left by a worker that died: it can be claimed again and
 * no longer counts as open, so it cannot hold a user's parse slots forever.
 *
 * @p table is always one of the job table names spelled in code, never input.
 */

#pragma once

#include <optional>
#include <string>

#include "database/Database.hpp"

namespace Repositories::JobRows {

/// A row `running` longer than this was left by a worker that died: every
/// provider call is capped well below it.
inline constexpr const char* kStaleRunning = "10 minutes";

/// Queued or running jobs one user may have per module before a new one is refused.
inline constexpr long kOpenMax = 3;

/// queued (or stale running) -> running, one more attempt. Returns the
/// attempt number (1 for the first run), or 0 when the row is not claimable.
inline int start(const char* table, const std::string& id) {
    return Database::get().execute_write([&](auto& txn) {
        auto r = txn.exec_params(std::string("UPDATE ") + table +
                                     " SET status = 'running', attempts = attempts + 1, started_at = now() "
                                     "WHERE id = $1::uuid AND (status = 'queued' OR (status = 'running' AND "
                                     " COALESCE(started_at, created_at) < now() - $2::interval)) RETURNING attempts",
                                 id,
                                 kStaleRunning);
        return r.empty() ? 0 : r[0][0].template as<int>();
    });
}

/// running -> queued, for an outage the queue will retry.
inline void requeue(const char* table, const std::string& id) {
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params(
            std::string("UPDATE ") + table + " SET status = 'queued' WHERE id = $1::uuid AND status = 'running'", id);
        return true;
    });
}

/// The row's status now; nullopt when the row is gone.
inline std::optional<std::string> status(const char* table, const std::string& id) {
    return Database::get().execute_read([&](auto& txn) -> std::optional<std::string> {
        auto r = txn.exec_params(std::string("SELECT status FROM ") + table + " WHERE id = $1::uuid", id);
        if (r.empty()) {
            return std::nullopt;
        }
        return r[0][0].template as<std::string>();
    });
}

/// The owner's jobs that are queued or running; a stale `running` row is not counted.
inline long open_count(const char* table, const std::string& owner) {
    return Database::get().execute_read([&](auto& txn) {
        auto r = txn.exec_params(std::string("SELECT count(*) FROM ") + table +
                                     " WHERE owner_id = $1::uuid AND (status = 'queued' OR (status = 'running' AND "
                                     " COALESCE(started_at, created_at) >= now() - $2::interval))",
                                 owner,
                                 kStaleRunning);
        return r[0][0].template as<long>();
    });
}

/// Appended to a finish: only the claim holder writes a result, so a worker
/// whose stale row was claimed again cannot overwrite the newer outcome.
inline constexpr const char* kWhileRunning = " AND status = 'running'";

/// Appended to a fail: a queued row may fail too (module off, no settings,
/// queue down), a done or failed one stays as it is.
inline constexpr const char* kWhileOpen = " AND status IN ('queued', 'running')";

}  // namespace Repositories::JobRows
