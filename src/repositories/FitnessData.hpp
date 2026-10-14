/**
 * @file FitnessData.hpp
 * @brief The rows a Xiaomi account brought in, removed in the caller's
 *        transaction: by an unlink that takes the data along, and by the
 *        deletion of the user who linked the account.
 *
 * The fitness tables are keyed by the Xiaomi account id (user_id), not by the
 * Life OS user, so a user delete does not reach them through owner_id.
 */

#pragma once

#include <string>

namespace Repositories::FitnessData {

/// The tables whose user_id is the Xiaomi account id.
inline constexpr const char* kTables[] = {"daily_activity",
                                          "sleep_sessions",
                                          "workouts",
                                          "body_measurements",
                                          "heart_rate_samples",
                                          "spo2_samples",
                                          "stress_samples",
                                          "abnormal_heart_beat_events"};

/// Deletes the account's fitness rows, sync runs and sync state.
template <typename Txn>
void delete_in(Txn& txn, const std::string& xiaomi_user_id) {
    for (const char* table : kTables) {
        txn.exec_params(std::string("DELETE FROM ") + table + " WHERE user_id = $1", xiaomi_user_id);
    }
    txn.exec_params("DELETE FROM sync_runs WHERE xiaomi_user_id = $1", xiaomi_user_id);
    txn.exec_params("DELETE FROM sync_state WHERE xiaomi_user_id = $1", xiaomi_user_id);
}

}  // namespace Repositories::FitnessData
