/**
 * @file Worker.cpp
 * @brief Bodies for src/jobs/Worker.hpp, compiled once into app_core.
 */

#include "jobs/Worker.hpp"

#include <exception>
#include <utility>

#include <spdlog/spdlog.h>

#include "jobs/Jobs.hpp"

namespace Jobs {

const char* outcome_label(Outcome outcome) {
    switch (outcome) {
        case Outcome::success:
            return "success";
        case Outcome::deferred:
            return "deferred";
        case Outcome::permanent_failure:
            return "permanent_failure";
        case Outcome::failure:
            return "failure";
    }
    return "failure";
}

RunResult run_one(JobQueue& queue, const Job& job, const Handler& handler) {
    RunResult out;
    {
        // The heartbeat lives exactly as long as the handler: it is stopped
        // (and its thread joined) before the outcome is written, so it never
        // renews the lease of a job that is already complete, failed or
        // requeued.
        LeaseHeartbeat heartbeat(queue, job.id, job.worker_id);
        try {
            out.result = handler(job);
        } catch (const DeferJob& e) {
            // Not a failure: the job waits (e.g. its row is held by another
            // delivery) and comes back without spending an attempt.
            out.outcome = Outcome::deferred;
            out.error = e.what();
            out.defer_ms = e.delay_ms;
        } catch (const PermanentJobError& e) {
            // No retries: straight to the DLQ, so a permanent mismatch can't
            // spin the worker through its whole retry budget.
            out.outcome = Outcome::permanent_failure;
            out.error = e.what();
        } catch (const std::exception& e) {
            out.outcome = Outcome::failure;
            out.error = e.what();
        } catch (...) {
            // A handler throwing a non-std type must not std::terminate the
            // worker thread (Dispatcher invites arbitrary handlers).
            out.outcome = Outcome::failure;
            out.error = "non-std exception in handler";
        }
        heartbeat.stop();
    }

    // The queue write sits outside the handler's try: a Redis error while
    // recording a success must not fail() the job and run it again (a second
    // email, a second webhook delivery).
    try {
        switch (out.outcome) {
            case Outcome::success:
                queue.complete(job.id, out.result);
                break;
            case Outcome::deferred:
                spdlog::info("Job deferred: id={} for {}ms: {}", job.id, out.defer_ms, out.error);
                queue.defer(job.id, out.defer_ms, out.error);
                break;
            case Outcome::permanent_failure:
                spdlog::error("Job permanently failed (no retry): id={} error={}", job.id, out.error);
                queue.dead_letter(job.id, out.error);
                break;
            case Outcome::failure:
                spdlog::error("Job failed: id={} error={}", job.id, out.error);
                queue.fail(job.id, out.error);
                break;
        }
    } catch (const std::exception& e) {
        // The blob still says processing and the id stays on the processing
        // list and its lease: the reaper hands it out again after the
        // visibility timeout (or startup recovery does), the at-least-once
        // contract, but never at once and never as a failure of a success.
        out.recorded = false;
        spdlog::error("Job {}: recording outcome {} failed: {}", job.id, outcome_label(out.outcome), e.what());
    }
    return out;
}

LeaseHeartbeat::LeaseHeartbeat(JobQueue& queue,
                               std::string job_id,
                               std::string worker_id,
                               std::chrono::milliseconds interval)
    : queue_(queue), job_id_(std::move(job_id)), worker_id_(std::move(worker_id)), interval_(interval) {
    const int64_t visibility = queue_.visibility_timeout_ms();
    if (visibility <= 0 || worker_id_.empty())
        return;  // leases off: nothing to renew
    if (interval_.count() <= 0)
        interval_ = std::chrono::milliseconds(visibility / 3);
    if (interval_ < std::chrono::milliseconds(100))
        interval_ = std::chrono::milliseconds(100);
    thread_ = std::thread([this] { loop_(); });
}

LeaseHeartbeat::~LeaseHeartbeat() {
    stop();
}

void LeaseHeartbeat::stop() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

int LeaseHeartbeat::renewals() const {
    std::lock_guard<std::mutex> lock(mu_);
    return renewals_;
}

void LeaseHeartbeat::loop_() {
    std::unique_lock<std::mutex> lock(mu_);
    for (;;) {
        if (cv_.wait_for(lock, interval_, [this] { return stopping_; }))
            return;
        lock.unlock();
        bool owned = true;
        try {
            owned = queue_.renew_lease(job_id_, worker_id_);
        } catch (const std::exception& e) {
            // Redis blip: the lease keeps its old deadline; try again next beat.
            spdlog::warn("Job {}: lease renewal failed: {}", job_id_, e.what());
        }
        lock.lock();
        if (!owned) {
            // The reaper took the job back or another worker owns it now.
            spdlog::warn("Job {}: lease no longer held by {}, renewal stopped", job_id_, worker_id_);
            return;
        }
        ++renewals_;
    }
}

bool RateLimitedLog::allow(std::chrono::steady_clock::time_point now) {
    if (logged_once_ && now - last_ < every_) {
        ++held_;
        return false;
    }
    suppressed_ = held_;
    held_ = 0;
    last_ = now;
    logged_once_ = true;
    return true;
}

Maintenance::Pass Maintenance::run(JobQueue& queue, std::chrono::steady_clock::time_point now) {
    Pass pass;
    auto report = [&](const char* step, const char* what) {
        ++pass.errors;
        if (log_.allow(now)) {
            if (log_.suppressed() > 0)
                spdlog::warn("Jobs maintenance: {} failed: {} ({} more failures since the last report)",
                             step,
                             what,
                             log_.suppressed());
            else
                spdlog::warn("Jobs maintenance: {} failed: {}", step, what);
        }
    };
    try {
        pass.promoted = queue.promote_due_jobs();
    } catch (const std::exception& e) {
        report("promote_due_jobs", e.what());
    }
    try {
        pass.reaped = queue.reap_expired_leases();
    } catch (const std::exception& e) {
        report("reap_expired_leases", e.what());
    }
    if (!trimmed_once_ || now - last_trim_ >= trim_every_) {
        trimmed_once_ = true;
        last_trim_ = now;
        try {
            pass.trimmed = queue.trim_indexes();
        } catch (const std::exception& e) {
            report("trim_indexes", e.what());
        }
        // Ids stranded in a processing list without a lease (another pod's,
        // which its own startup recovery never reads): as rare as the trim.
        try {
            pass.orphans = queue.recover_orphans();
        } catch (const std::exception& e) {
            report("recover_orphans", e.what());
        }
    }
    return pass;
}

}  // namespace Jobs
