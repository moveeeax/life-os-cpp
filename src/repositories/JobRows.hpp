/**
 * @file JobRows.hpp
 * @brief The life cycle the LLM job tables share (food, money and tasks parse
 *        jobs, money advisor reports): the claim, the requeue, what counts as
 *        open, and the status a worker reads back.
 *
 * A row goes queued -> running (one more attempt) -> done | failed, or back to
 * queued for an outage the queue retries. A `running` row older than
 * kStaleRunning was left by a worker that died: it can be claimed again, it
 * no longer counts as open (nor does a queued row that old), and a reader
 * sees it as failed. Every write after the claim is fenced by the attempt
 * start() returned, so only the current claim holder changes the row.
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

/// running -> queued, for an outage the queue will retry; only by the claim
/// holder of @p attempt (a worker whose stale row was claimed again is late).
inline void requeue(const char* table, const std::string& id, int attempt) {
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params(std::string("UPDATE ") + table +
                            " SET status = 'queued' WHERE id = $1::uuid AND status = 'running' AND attempts = $2",
                        id,
                        attempt);
        return true;
    });
}

/// Milliseconds until a `running` row turns stale and can be claimed again,
/// plus a second of margin; 0 when the row is not running (or already stale).
inline long long stale_in_ms(const char* table, const std::string& id) {
    return Database::get().execute_read([&](auto& txn) -> long long {
        auto r = txn.exec_params(
            std::string("SELECT GREATEST(0, CEIL(EXTRACT(EPOCH FROM (COALESCE(started_at, created_at) + "
                        "$2::interval - now())) * 1000))::bigint FROM ") +
                table + " WHERE id = $1::uuid AND status = 'running'",
            id,
            kStaleRunning);
        if (r.empty()) {
            return 0;
        }
        const auto ms = r[0][0].template as<long long>();
        return ms > 0 ? ms + 1000 : 0;
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

/// The owner's jobs that are queued or running. A row untouched for longer than
/// kStaleRunning is not counted, queued or running: a dead worker, or a queue
/// job lost or given up on, must not hold the user's parse slots forever.
/// (A requeued row keeps its last started_at, so a retry waiting out its
/// backoff, at most a minute, still counts.)
inline long open_count(const char* table, const std::string& owner) {
    return Database::get().execute_read([&](auto& txn) {
        auto r = txn.exec_params(std::string("SELECT count(*) FROM ") + table +
                                     " WHERE owner_id = $1::uuid AND status IN ('queued', 'running') AND "
                                     " COALESCE(started_at, created_at) >= now() - $2::interval",
                                 owner,
                                 kStaleRunning);
        return r[0][0].template as<long>();
    });
}

/// Appended to a finish, with the attempt start() returned as parameter $n:
/// only the claim holder writes a result, so a worker whose stale row was
/// claimed again cannot overwrite the newer outcome, whatever it is.
inline std::string while_claimed(int n) {
    return " AND status = 'running' AND attempts = $" + std::to_string(n) + "::int";
}

/// Appended to a fail, with the attempt as parameter $n. Attempt 0 means the
/// job failed before its claim (module off, no settings, queue down, queue
/// gave up): a queued or running row fails. After a claim only the holder's
/// own running row does. A done or failed row stays as it is.
inline std::string while_open(int n) {
    const std::string p = "$" + std::to_string(n) + "::int";
    return " AND ((" + p + " = 0 AND status IN ('queued', 'running')) OR (status = 'running' AND attempts = " + p +
           "))";
}

/// The status as a reader sees it: a `running` row past kStaleRunning was left
/// by a worker that died, and a page polling it would wait forever. If the
/// queue still holds the job it is claimed again and turns `running` anew.
inline std::string shown_status() {
    return std::string("CASE WHEN status = 'running' AND COALESCE(started_at, created_at) < now() - interval '") +
           kStaleRunning + "' THEN 'failed' ELSE status END";
}

/// The error next to shown_status(): `worker_lost` for such a row.
inline std::string shown_error() {
    return std::string("CASE WHEN status = 'running' AND COALESCE(started_at, created_at) < now() - interval '") +
           kStaleRunning + "' THEN 'worker_lost: the worker stopped while running the job' ELSE error END";
}

}  // namespace Repositories::JobRows
