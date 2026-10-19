/**
 * @file Jobs.hpp
 * @brief Redis-backed job queue module
 * @details Provides job submission, retrieval, and lifecycle management
 *          using Redis lists (LPUSH/BRPOP) for distributed work queues.
 *
 * Declarations only — the bodies live in Jobs.cpp (compiled once into
 * app_core; docs/ARCHITECTURE.md §4): including this header no
 * longer pulls redis++/spdlog/Cache/Trace into the including TU. The
 * nlohmann json alias arrives via jobs/Job.hpp.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "jobs/Job.hpp"

namespace sw {
namespace redis {
class Redis;
}  // namespace redis
}  // namespace sw

namespace Jobs {

/**
 * @brief Job queue manager backed by Redis
 *
 * Exception contract (for callers adding new endpoints):
 *   - submit / pick / complete / fail / cancel / get_status / list_paged /
 *     list_dlq / requeue_from_dlq THROW on Redis errors or missing state —
 *     controllers map them to 5xx.
 *   - promote_due_jobs / reap_expired_leases / trim_indexes / renew_lease
 *     throw when their first read fails; the worker's maintenance pass and
 *     its lease heartbeat log that (jobs/Worker.hpp). A failure on one id
 *     inside promote or reap is logged and left for the next pass.
 *   - dlq_depth / dlq_depth_by_type / health_check / set_trace_id and the
 *     index ZADDs inside submit are best-effort: they log and swallow Redis
 *     errors (observability must not take the queue down with it).
 */
/// Runs a job gets before the queue dead-letters it (the initialize() default).
inline constexpr int kDefaultMaxRetries = 3;

class JobQueue {
private:
    bool initialized_ = false;
    long result_ttl_ = 86400;  // seconds
    int default_max_retries_ = kDefaultMaxRetries;
    // Retry backoff (JOBS_RETRY_BACKOFF_BASE_MS, 2000 ms by default in Core).
    // 0 = legacy behaviour: fail() requeues immediately.
    // >0 = a failed job is parked in jobs:delayed for an exponentially growing
    // delay (base * 2^(retry_count-1), capped at max), promoted back by
    // promote_due_jobs() — kills the tight pick→fail→pick loop on a near-empty
    // queue.
    int64_t retry_backoff_base_ms_ = 0;
    int64_t retry_backoff_max_ms_ = 60000;
    // Visibility timeout (opt-in). 0 = leases disabled (only same-worker startup
    // recovery). >0 = pick() leases the job in jobs:leases for this many ms;
    // reap_expired_leases() reclaims any lease a crashed worker never cleared,
    // re-running it via fail() (so a permanently-stuck job eventually DLQs).
    int64_t visibility_timeout_ms_ = 0;
    // shared_ptr (not unique_ptr) so pick() can hold a local copy for the
    // duration of its BRPOP: shutdown() dropping the manager's reference then
    // can't free the client out from under an in-flight blocking pop.
    // sw::redis::Redis is forward-declared above — construction/destruction
    // sites live in Jobs.cpp, and shared_ptr's type-erased deleter keeps the
    // implicit ~JobQueue() valid against the incomplete type.
    std::shared_ptr<sw::redis::Redis> blocking_client_;  // for BRPOP (long socket_timeout)
    // Where the next multi-type pick starts its round over the types; every
    // call advances it, so no type is always asked first (move_one_of_).
    std::atomic<std::size_t> pick_rotation_{0};

public:
    void initialize(long result_ttl = 86400, int max_retries = kDefaultMaxRetries);

    /**
     * @brief Enable exponential retry backoff. base_ms=0 disables it (legacy
     *        immediate requeue). Called once during init from config.
     */
    void set_retry_backoff(int64_t base_ms, int64_t max_ms);

    /**
     * @brief Enable the visibility-timeout lease. sec<=0 disables it. Called
     *        once during init from config.
     */
    void set_visibility_timeout(int64_t sec) { visibility_timeout_ms_ = sec <= 0 ? 0 : sec * 1000; }

    /// The lease length in ms (0 = leases off). The worker renews a running
    /// job's lease every third of it (Jobs::LeaseHeartbeat).
    int64_t visibility_timeout_ms() const { return visibility_timeout_ms_; }

    /**
     * @brief Push the lease of a running job one visibility timeout ahead.
     * @details One Lua script: the lease moves only while the blob still says
     *          `processing` by @p worker_id and the lease entry still exists.
     *          A job the reaper took back (lease gone) or another worker picked
     *          (worker_id changed) is left alone, so a slow handler can never
     *          keep a job another worker now owns. No-op with leases off.
     * @return true when the lease was moved. Throws on Redis errors.
     */
    bool renew_lease(const std::string& id, const std::string& worker_id);

    /**
     * @brief Dedicated standalone Redis client for blocking BRPOP. The Cache
     *        pool has too tight a socket_timeout for blocking ops; this one
     *        uses (brpop_timeout + 2s) so the socket doesn't trip first.
     */
    void init_blocking_client(const std::string& host,
                              int port,
                              long brpop_timeout_sec,
                              const std::string& password = "",
                              int pool_size = 4,
                              int db = 0);

    /**
     * @brief Dedicated Sentinel-aware Redis client for blocking BRPOP.
     */
    void init_blocking_client_sentinel(const std::string& master_name,
                                       const std::vector<std::pair<std::string, int>>& sentinels,
                                       long brpop_timeout_sec,
                                       const std::string& password = "",
                                       const std::string& sentinel_password = "",
                                       int pool_size = 4,
                                       int db = 0);

    /**
     * @brief Submit a new job to the queue
     */
    Job submit(const std::string& type, const json& payload, int max_retries = -1);

    /**
     * @brief Pick the next job from one or more queues (blocking pop).
     * @details The id moves from the live queue into the worker's per-worker
     *          processing list in one Redis command (BRPOPLPUSH, or RPOPLPUSH
     *          for a queue that is not empty), so a worker crashing between
     *          "got id" and "set status=processing" doesn't lose the job:
     *          recover_processing() on startup pushes leftovers back. With
     *          one type that is a single BRPOPLPUSH for the whole timeout;
     *          with several, the types are tried in rotation in one-second
     *          slices (a non-blocking pass over all of them, then a blocking
     *          wait on one), so the move stays atomic and a busy type cannot
     *          starve the others, at the cost of up to a slice of latency.
     * @param types Queue types to pop from (empty = nothing to pick)
     * @param timeout_sec How long to wait in all (0 = block forever)
     * @param worker_id Worker claiming this job
     * @return Job if one was available, nullopt on timeout
     */
    std::optional<Job> pick(const std::vector<std::string>& types, long timeout_sec, const std::string& worker_id = "");

    /**
     * @brief Mark a job as completed with result
     */
    void complete(const std::string& id, const json& result);

    /**
     * @brief Mark a job as failed; requeue if retries remain
     */
    void fail(const std::string& id, const std::string& error);

    /**
     * @brief Park a picked job for @p delay_ms without counting an attempt
     *        (a handler threw DeferJob): it leaves the worker's processing
     *        list, goes back to `pending` in the delayed set, and
     *        promote_due_jobs() returns it to its queue when due. Works with
     *        retry backoff off too. A delay below 1 s is raised to 1 s.
     */
    void defer(const std::string& id, int64_t delay_ms, const std::string& reason);

    /**
     * @brief Send a job STRAIGHT to the dead-letter queue, bypassing the retry
     *        budget — for permanent failures (PermanentJobError, e.g. no handler
     *        for the type) where retrying only burns tight pick→throw→requeue
     *        cycles. Mirrors fail()'s DLQ branch without the retry path.
     */
    void dead_letter(const std::string& id, const std::string& error);

    /**
     * @brief List jobs currently in the dead-letter queue.
     * @param type Filter by job type (empty = all DLQ).
     * @param limit Max results.
     */
    std::vector<Job> list_dlq(const std::string& type = "", int limit = 100);

    /**
     * @brief Move a DLQ job back to its live queue, resetting retry_count.
     * @return true if the job was found and requeued; false otherwise.
     */
    bool requeue_from_dlq(const std::string& id);

    /**
     * @brief Current total DLQ depth across all types.
     */
    long dlq_depth();

    /**
     * @brief Depth per job type. Scans each `jobs:dlq:*` list to return
     *        {type → depth}. Used by the metrics refresher so dashboards
     *        can see which specific job type is clogged.
     */
    std::unordered_map<std::string, long> dlq_depth_by_type();

    /**
     * @brief Live depth per job type for the WAITING queue (`jobs:queue:*`),
     *        as {type → depth}. The leading-indicator counterpart to
     *        dlq_depth_by_type (which is the lagging "already gave up" signal):
     *        a climbing queue depth means submitters are outrunning the
     *        worker pool before anything reaches the DLQ. Used by the metrics
     *        refresher so dashboards/alerts can see a backlog forming.
     */
    std::unordered_map<std::string, long> queue_depth_by_type();

    /**
     * @brief Return any jobs left in this worker's processing list to the
     *        live queue. Call once at worker startup: if the previous
     *        instance with the same WORKER_ID crashed between BRPOPLPUSH
     *        and complete()/fail(), the id stranded in jobs:processing:*
     *        gets rescued and re-executed.
     * @return number of jobs rescued.
     */
    long recover_processing(const std::string& worker_id);

    /**
     * @brief Move every job whose backoff or defer window has elapsed from the
     *        delayed set back onto its live queue. Runs whether or not retry
     *        backoff is on (a deferred job waits there too); an empty set
     *        costs one ZRANGEBYSCORE. Call periodically from the worker loop.
     *        Each id leaves the delayed set and lands on its queue in one Lua
     *        script, so a Redis error between the two cannot strand it.
     * @param now_ms the clock to compare due times with (-1 = the wall
     *        clock); tests pass an explicit time instead of sleeping.
     * @return number of jobs promoted. Throws when the delayed set cannot be
     *         read (the worker's maintenance pass logs it); a failure on one
     *         id is logged and leaves that id for the next pass.
     */
    long promote_due_jobs(int64_t now_ms = -1);

    /**
     * @brief Reclaim jobs whose visibility lease has expired — a worker picked
     *        them and died without complete()/fail(), so no same-id startup
     *        recovery will ever rescue them. Each is run back through fail()
     *        (bumps retry, requeues or DLQs), so a permanently-wedged job is not
     *        reaped forever. Cheap no-op when the lease is disabled. Call
     *        periodically from the worker loop.
     *        The gate is one Lua script that removes the lease only while
     *        its score is still past @p now_ms, so a lease the worker renewed
     *        a moment ago is not reaped. When fail() then throws, the lease is
     *        put back as already expired and the next pass tries again.
     * @param now_ms the clock (-1 = the wall clock).
     * @return number of leases reclaimed. Throws when the lease set cannot be
     *         read.
     */
    long reap_expired_leases(int64_t now_ms = -1);

    /**
     * @brief Drop jobs:index and every jobs:index:<type> entry created more
     *        than result_ttl ago. submit() adds to these sets and only
     *        list_paged healed them, page by page, so without this they grew
     *        with every job ever submitted. The blob of such a job has expired
     *        (completed or cancelled) or still sits in the DLQ, which keeps its
     *        own listing. Called from the worker's maintenance pass.
     * @param now_sec the clock in seconds (-1 = the wall clock).
     * @return entries removed. Throws on Redis errors.
     */
    long trim_indexes(int64_t now_sec = -1);

    /**
     * @brief Get job status by ID
     */
    std::optional<Job> get_status(const std::string& id);

    /**
     * @brief Cancel a job (sets status to failed with "cancelled" error).
     *        The status check and the write are one compare-and-set in Redis:
     *        of concurrent cancels of one job exactly one returns true. The id
     *        leaves its live queue, the delayed set and its worker's
     *        processing list, so nothing runs it afterwards.
     */
    bool cancel(const std::string& id);

    /**
     * @brief One page of the time-ordered job listing plus the index total.
     */
    struct JobPage {
        std::vector<Job> jobs;
        long total = 0;
    };

    /**
     * @brief List jobs newest-first with real offset pagination, backed by
     *        the jobs:index sorted sets (score = created_at).
     * @param type Filter by job type (empty = all types).
     * @param limit Page size.
     * @param offset 0-based start position.
     * @details Index entries whose blob has expired (result_ttl) are healed
     *          lazily: dropped from the queried index as pages touch them,
     *          so `total` may transiently overcount until old pages are
     *          visited. Jobs submitted before the index existed are not
     *          listed (they age out within result_ttl anyway).
     */
    JobPage list_paged(const std::string& type = "", int limit = 20, int offset = 0);

    /**
     * @brief Record the worker's processing-span trace id on the job blob so
     *        UIs can deep-link into the tracing backend. Best-effort no-op
     *        when the job is gone.
     */
    void set_trace_id(const std::string& id, const std::string& trace_id);

    /**
     * @brief Health check — ping Redis
     */
    bool health_check();

    // Ordering: worker_main joins its threads before Core::shutdown() reaches
    // Jobs::shutdown(). Even so, pick() holds a local shared_ptr copy of
    // blocking_client_ for its BRPOP, so dropping the manager's reference here
    // can't free the client mid-pop — the worst case is a benign refcount race,
    // not a use-after-free on the Redis object.
    void shutdown();

    bool is_initialized() const { return initialized_; }
    long result_ttl() const { return result_ttl_; }
    int default_max_retries() const { return default_max_retries_; }

private:
    void check_initialized() const;

    /**
     * @brief The multi-type half of pick(): move one id from the first
     *        non-empty queue among @p types into @p proc_list, one
     *        RPOPLPUSH / BRPOPLPUSH at a time (see the body for the rotation
     *        and the slices). nullopt once @p timeout_sec has passed.
     */
    std::optional<std::string> move_one_of_(sw::redis::Redis& redis,
                                            const std::vector<std::string>& types,
                                            const std::string& proc_list,
                                            long timeout_sec);

    // Drop a job's visibility lease (no-op when the feature is off or the lease
    // is already gone). Called from every terminal/transition path so the reaper
    // never resurrects a job that already moved on.
    void clear_lease_(sw::redis::Redis& redis, const std::string& id);

    /**
     * @brief Load the job blob for @p id in one GET. Returns nullopt when the
     *        key is missing; JSON parse errors propagate (nlohmann exceptions),
     *        exactly like the inlined get→parse it replaces.
     */
    std::optional<Job> load_job_(sw::redis::Redis& redis, const std::string& id);

    /// load_job_, but a missing blob throws — the "Job not found" contract
    /// complete()/fail()/dead_letter() expose to controllers.
    Job load_job_or_throw_(sw::redis::Redis& redis, const std::string& id);

    /// Persist the job blob (plain SET, no TTL) — the write half of load_job_.
    void save_job_(sw::redis::Redis& redis, const Job& job);

    // Drop the job from its worker's processing list (best-effort) and release
    // its visibility lease — the shared terminal-transition cleanup of
    // complete()/fail()/dead_letter(). cancel() keeps its own variant (it also
    // clears the pending queue and gates the lrem on was_processing).
    void release_from_worker_(sw::redis::Redis& redis, const Job& job);

    // completed, dead or failed (cancelled): a blob in one of these states is
    // never run again, whatever list its id turns up on.
    static bool is_terminal_status_(const std::string& status) {
        return status == "completed" || status == "dead" || status == "failed";
    }

    /**
     * @brief SCAN every list under @p prefix and return {type → LLEN}, where
     *        type is the key with the prefix stripped. Best-effort: swallows
     *        Redis errors ("<log_ctx>: scan failed"). Shared body of
     *        dlq_depth_by_type() / queue_depth_by_type().
     * @param skip_key Exact key to skip (the DLQ `_all` index set; "" = none).
     */
    std::unordered_map<std::string, long> depth_by_prefix_(const std::string& prefix,
                                                           const std::string& skip_key,
                                                           const char* log_ctx);

    // Backoff delay for a job that just failed its @p retry_count-th attempt
    // (>=1). Exponential (base * 2^(retry_count-1)), capped at the configured
    // max. Returns 0 when backoff is disabled → caller requeues immediately.
    int64_t backoff_delay_ms_(int retry_count) const;

    /**
     * @brief Load the Job rows for @p ids in a single MGET round-trip.
     *        Missing or unparseable entries are skipped silently — unless
     *        @p stale is given, in which case their ids are collected there
     *        (list_paged uses that to heal expired index entries).
     */
    std::vector<Job> fetch_jobs_by_ids_(const std::vector<std::string>& ids, std::vector<std::string>* stale = nullptr);
};

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
// The global JobQueue instance (global_jobs) lives in Jobs.cpp; these
// functions are its only doorway — mirrors Billing::'s singleton shape in
// src/billing/PayPalClient.cpp.

void initialize(long result_ttl = 86400, int max_retries = kDefaultMaxRetries);

JobQueue& get();

bool is_initialized();

void shutdown();

}  // namespace Jobs
