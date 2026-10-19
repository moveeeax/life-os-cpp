/**
 * @file Worker.hpp
 * @brief The worker's per-job and per-cycle logic, out of worker_main.cpp so
 *        the tests can drive it against Redis: run_one() maps a handler's
 *        outcome onto the queue, LeaseHeartbeat renews a running job's lease,
 *        Maintenance runs the promote / reap / trim pass and logs its
 *        failures without flooding the log.
 *
 * Bodies in Worker.cpp (app_core). worker_main.cpp keeps the process
 * concerns: threads, tracing spans, Prometheus metrics, health endpoints.
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "jobs/Job.hpp"

namespace Jobs {

class JobQueue;

/// How a run ended, as recorded on the queue (and as the metrics label).
enum class Outcome {
    success,            ///< handler returned; complete() was called
    deferred,           ///< handler threw DeferJob; defer() was called
    permanent_failure,  ///< handler threw PermanentJobError; dead_letter() was called
    failure,            ///< handler threw anything else; fail() was called
};

/// The Prometheus label of an outcome ("success", "deferred", ...).
const char* outcome_label(Outcome outcome);

struct RunResult {
    Outcome outcome = Outcome::success;
    /// The handler's error text (empty on success).
    std::string error;
    /// The DeferJob delay, for the deferred outcome.
    int64_t defer_ms = 0;
    /// False when the queue write for the outcome threw (Redis error). The
    /// job then stays on the worker's processing list and its lease, so the
    /// reaper or startup recovery runs it again: at least once, never a
    /// second fail() of a job that succeeded.
    bool recorded = true;
    /// The handler's result, for the success outcome.
    json result;
};

using Handler = std::function<json(const Job&)>;

/**
 * @brief Run @p handler on a picked @p job and record the outcome on
 *        @p queue. The handler's exceptions map to the queue calls:
 *        DeferJob → defer(), PermanentJobError → dead_letter(), any other
 *        std::exception or a non-std throw → fail(). complete() runs outside
 *        the handler's try, so a Redis error while recording a success never
 *        fails the job and runs it again. While the handler runs, a
 *        LeaseHeartbeat keeps the job's lease alive (when leases are on).
 *        Never throws.
 */
RunResult run_one(JobQueue& queue, const Job& job, const Handler& handler);

/**
 * @brief Renews the lease of one running job every @p interval on its own
 *        thread, from construction until stop() or destruction. A renewal
 *        that finds the job no longer this worker's (the reaper took it back,
 *        another worker picked it) stops the heartbeat: it never extends a
 *        lease it does not own. Does nothing with leases off.
 */
class LeaseHeartbeat {
public:
    /// interval <= 0 picks a third of the queue's visibility timeout.
    LeaseHeartbeat(JobQueue& queue,
                   std::string job_id,
                   std::string worker_id,
                   std::chrono::milliseconds interval = std::chrono::milliseconds(0));
    ~LeaseHeartbeat();
    LeaseHeartbeat(const LeaseHeartbeat&) = delete;
    LeaseHeartbeat& operator=(const LeaseHeartbeat&) = delete;

    /// Stop renewing and join the thread. Idempotent.
    void stop();

    /// Renewals that moved the lease so far.
    int renewals() const;

private:
    void loop_();

    JobQueue& queue_;
    std::string job_id_;
    std::string worker_id_;
    std::chrono::milliseconds interval_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    bool stopping_ = false;
    int renewals_ = 0;
    std::thread thread_;
};

/**
 * @brief Lets one message through per @p every, counting the ones held back.
 *        Not thread-safe; each worker thread owns its own.
 */
class RateLimitedLog {
public:
    explicit RateLimitedLog(std::chrono::seconds every) : every_(every) {}

    /// True when a message may be logged at @p now; then suppressed() holds
    /// how many were held back since the last one.
    bool allow(std::chrono::steady_clock::time_point now);
    long suppressed() const { return suppressed_; }

private:
    std::chrono::seconds every_;
    bool logged_once_ = false;
    std::chrono::steady_clock::time_point last_{};
    long held_ = 0;
    long suppressed_ = 0;
};

/**
 * @brief The worker loop's maintenance pass: promote due jobs and reap
 *        expired leases on every cycle, trim the listing indexes once per
 *        @p trim_every. A failure is logged at warn, at most once a minute
 *        per worker thread with the count of the failures in between, instead
 *        of being swallowed. Never throws.
 */
class Maintenance {
public:
    explicit Maintenance(std::chrono::seconds trim_every = std::chrono::minutes(10)) : trim_every_(trim_every) {}

    struct Pass {
        long promoted = 0;
        long reaped = 0;
        long trimmed = 0;
        int errors = 0;
    };

    Pass run(JobQueue& queue, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

private:
    std::chrono::seconds trim_every_;
    bool trimmed_once_ = false;
    std::chrono::steady_clock::time_point last_trim_{};
    RateLimitedLog log_{std::chrono::seconds(60)};
};

}  // namespace Jobs
