/**
 * @file SyncService.hpp
 * @brief Sync orchestration: range chunks, counters, journal, type isolation.
 *
 * One run per account: the queued -> running transition is atomic via the
 * partial unique index on sync_runs; a competing run ends with status skipped
 * without reaching the cloud. One type's failure does not cancel the rest: the
 * result carries counters or an error class (auth / protocol / timeout / other)
 * per type.
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fitness/xiaomi/Credentials.hpp"
#include "fitness/xiaomi/HttpTransport.hpp"

namespace Sync {

/// Canonical type order, same as the reference.
inline const std::vector<std::string> kAllDataTypes = {
    "daily_activity", "heart_rate", "body_measurements", "sleep", "workouts", "spo2", "stress", "abnormal_heart_beat"};

class SyncService {
public:
    /// @param on_rotate persists a rotated token; empty in tests.
    SyncService(Xiaomi::HttpTransport& transport,
                Xiaomi::Credentials credentials,
                std::function<void(const Xiaomi::Credentials&)> on_rotate = {});

    /**
     * @brief Execute run run_id over a date range for the listed types.
     *
     * Moves the run to running itself, writes the result and final status to
     * sync_runs, updates sync_state for successful types. Returns the per-type
     * result. Every outcome of the cloud side is reflected in the journal; a
     * database error while claiming the run propagates (the queue retries).
     *
     * The run's own row may already be `running` when its job is delivered
     * again (the worker that ran it was killed). A row whose heartbeat has gone
     * quiet is taken over; one that still beats is left to its holder and
     * start_outcome() says HeldElsewhere, with retry_in_ms() until it would turn
     * quiet. While it runs, every request to the cloud refreshes the heartbeat
     * (at most every kHeartbeatEverySeconds).
     */
    nlohmann::json run(long run_id,
                       const std::string& from,
                       const std::string& to,
                       const std::vector<std::string>& data_types);

    /// Outcome of the login of the last run(): accepted, refused by Xiaomi
    /// (only a new link fixes that), or not attempted (skipped run, network
    /// failure before an answer).
    enum class Login { NotAttempted, Accepted, Refused };
    Login login_outcome() const { return login_outcome_; }

    /// How the last run() began: it ran, it was skipped (another run of the
    /// account holds the slot, or the run is already closed), or its own row
    /// is running under another delivery that is still alive.
    enum class Start { Started, Skipped, HeldElsewhere };
    Start start_outcome() const { return start_outcome_; }
    /// For HeldElsewhere: milliseconds until that row turns quiet and can be taken over.
    long long retry_in_ms() const { return retry_in_ms_; }

    /// The heartbeat of a running row is refreshed at most this often.
    static constexpr long kHeartbeatEverySeconds = 30;

    /// A running row whose heartbeat is older than this was left by a worker
    /// that is gone: three request timeouts plus two minutes, so one slow
    /// request between two beats never makes a live run look quiet.
    static long quiet_after_seconds();

    /// A running row without a heartbeat (started before migration 047) is
    /// stale after the per-type ceiling times the number of types plus a margin.
    static long stale_after_seconds();

private:
    Xiaomi::HttpTransport& transport_;
    Xiaomi::Credentials credentials_;
    std::function<void(const Xiaomi::Credentials&)> on_rotate_;
    Login login_outcome_ = Login::NotAttempted;
    Start start_outcome_ = Start::Skipped;
    long long retry_in_ms_ = 0;
};

}  // namespace Sync
