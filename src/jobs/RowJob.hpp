/**
 * @file RowJob.hpp
 * @brief The life cycle a job backed by a database row runs through (the food,
 *        money, tasks and unified parses, the money advisor): the claim, the
 *        wait for a row another delivery holds, the retry limit, the failed and
 *        retried outcomes, and the catch that keeps a claimed row from staying
 *        `running`.
 *
 * Duck-typed over the row repository, so jobs/ stays a leaf: @p Rows has
 * start(id), status(id), stale_in_ms(id), requeue(id, attempt) and
 * fail(id, code, message, attempt), as Repositories::JobRows provides them.
 *
 * A handler builds one RowJob, fails through failed() before the claim (module
 * off, no settings), calls claim(), and runs its provider call and finish in
 * run(). Inside run(), an outage goes through give_up_or_retry(): the row is
 * queued again and RowRetry tells the queue to retry, until the last attempt
 * closes the row as failed instead.
 */

#pragma once

#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "jobs/Job.hpp"
#include "jobs/Jobs.hpp"

namespace Jobs {

/// Thrown by RowJob::give_up_or_retry after the row was queued again: the
/// queue retries the job. what() is "<job type>: <reason>".
struct RowRetry : std::runtime_error {
    explicit RowRetry(const std::string& what) : std::runtime_error(what) {}
};

/// The retry limit the job was submitted with; the worker's own setting is the
/// fallback for a job enqueued by an older API.
inline int max_attempts_of(const nlohmann::json& payload) {
    if (payload.contains("max_attempts") && payload["max_attempts"].is_number_integer()) {
        return payload["max_attempts"].get<int>();
    }
    return Jobs::is_initialized() ? Jobs::get().default_max_retries() : Jobs::kDefaultMaxRetries;
}

template <typename Rows>
class RowJob {
public:
    /// @param type   The job type ("food_parse"), the prefix of what a retry throws.
    /// @param label  The log label ("food parse").
    /// @param id_key The key of the row id in the answer ("job_id", "report_id").
    RowJob(const char* type,
           const char* label,
           const char* id_key,
           std::string id,
           Rows& rows,
           const nlohmann::json& payload)
        : type_(type)
        , label_(label)
        , id_key_(id_key)
        , id_(std::move(id))
        , rows_(rows)
        , max_attempts_(max_attempts_of(payload)) {}

    const std::string& id() const { return id_; }

    /// What start() returned: 0 until the claim, so a fail before it is not
    /// fenced by an attempt.
    int attempt() const { return attempt_; }

    /// Close the row as failed (fenced by the attempt) and say so.
    nlohmann::json failed(const std::string& code, const std::string& message) {
        rows_.fail(id_, code, message, attempt_);
        spdlog::warn("{} {}: {}", label_, id_, code);
        return nlohmann::json{{id_key_, id_}, {"status", "failed"}, {"error", code}};
    }

    /**
     * queued (or stale running) -> running, one more attempt. Returns nullopt
     * when this call holds the row now, else the answer for a row that is not
     * claimable (done, failed, gone).
     * @throws DeferJob while the row is `running` for another delivery, or for
     *         a worker that died less than JobRows::kStaleRunning ago: the job
     *         waits until the row turns stale, without spending an attempt.
     */
    std::optional<nlohmann::json> claim() {
        attempt_ = rows_.start(id_);
        if (attempt_ != 0) {
            return std::nullopt;
        }
        const auto now = rows_.status(id_);
        if (now == "running") {
            throw DeferJob(std::string(type_) + ": the row is running elsewhere", rows_.stale_in_ms(id_));
        }
        return nlohmann::json{{id_key_, id_}, {"status", now.value_or("missing")}};
    }

    /// A row that is `running` must end the call as done, failed or queued
    /// again: the page polls it, and a redelivery cannot claim it.
    /// @throws RowRetry after queueing the row again, before the last attempt.
    nlohmann::json give_up_or_retry(const char* code, const std::string& what) {
        if (attempt_ >= max_attempts_) {
            return failed(code, what);
        }
        rows_.requeue(id_, attempt_);
        throw RowRetry(std::string(type_) + ": " + what);
    }

    /// @p body() after the claim. Anything it throws but a retry (a database
    /// error, an unexpected shape) leaves the row `running`, so it goes through
    /// give_up_or_retry as internal_error.
    template <typename Body>
    nlohmann::json run(Body&& body) {
        try {
            return body();
        } catch (const RowRetry&) {
            throw;
        } catch (const std::exception& e) {
            return give_up_or_retry("internal_error", e.what());
        }
    }

private:
    const char* type_;
    const char* label_;
    const char* id_key_;
    std::string id_;
    Rows& rows_;
    int max_attempts_;
    int attempt_ = 0;
};

}  // namespace Jobs
