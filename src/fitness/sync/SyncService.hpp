/**
 * @file SyncService.hpp
 * @brief Sync orchestration: range chunks, counters, journal, type isolation.
 *
 * One run per system: the queued -> running transition is atomic via the
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
     * result. Does not throw: every outcome of the run is reflected in the journal.
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

private:
    Xiaomi::HttpTransport& transport_;
    Xiaomi::Credentials credentials_;
    std::function<void(const Xiaomi::Credentials&)> on_rotate_;
    Login login_outcome_ = Login::NotAttempted;
};

}  // namespace Sync
