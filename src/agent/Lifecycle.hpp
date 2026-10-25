/**
 * @file Lifecycle.hpp
 * @brief The life of a user's agent pod. The API flips the row and queues a
 *        job (wake, request_stop); the worker does the Kubernetes and S3 work
 *        in three jobs, each idempotent and re-run with DeferJob while it
 *        waits on the cluster:
 *          - ani_agent_start: Secret + Pod for a starting row, then waits for
 *            the pod to be ready and marks the row running;
 *          - ani_agent_stop: deletes the pod, waits until it is gone (its
 *            keeper saved the next snapshot by then), keeps the snapshot if
 *            it arrived, deletes the Secret, marks the row stopped;
 *          - ani_agents_reconcile (every minute): stops idle, old and dead
 *            agents, restarts lost stop jobs, deletes pods no row owns.
 *        Payload of start and stop: {"owner": "<user id>"}.
 */

#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace Agent {

inline constexpr const char* kStartJob = "ani_agent_start";
inline constexpr const char* kStopJob = "ani_agent_stop";
inline constexpr const char* kReconcileJob = "ani_agents_reconcile";

/// A start that is not ready after this long is stopped as failed.
inline constexpr int kStartLimitSeconds = 420;
/// A stopping row whose job seems lost gets a new stop job after this long.
inline constexpr int kStopRetrySeconds = 120;
/// A pod that ignores its grace period this long after the delete is killed.
inline constexpr int kStopForceSeconds = 300;

enum class Wake {
    Started,    // the pod is on its way
    AlreadyUp,  // starting or running already; the idle timer was pushed back
    Busy,       // max_running pods already run
    Off,        // the agent is switched off or not configured
};

/// The user came to Ani: start their agent, or keep it alive.
Wake wake(const std::string& owner);

/// Stop the user's agent if it runs (logout, the stop button). @p error lands in
/// last_error when the stop is a failure. Never throws: logout must not fail on it.
void request_stop(const std::string& owner, const std::string& error = "");

/// The job handlers (registered in agent/jobs/Register.cpp).
nlohmann::json process_start(const nlohmann::json& payload);
nlohmann::json process_stop(const nlohmann::json& payload);
nlohmann::json process_reconcile(const nlohmann::json& payload);

}  // namespace Agent
