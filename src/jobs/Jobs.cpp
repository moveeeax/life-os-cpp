/**
 * @file Jobs.cpp
 * @brief Bodies for src/jobs/Jobs.hpp — compiled once into app_core. This is
 *        the only jobs TU that sees redis++/spdlog/Cache/Trace; the header no
 *        longer exposes them to including TUs. Every contract and its
 *        rationale is documented on the declarations in the header; the
 *        comments here explain the Redis mechanics in place.
 *
 * Delivery contract: at least once. A job runs to the end at least once
 * unless it is cancelled or dead-lettered, and it may run more than once:
 * a worker that dies after the handler and before complete() lands, a lease
 * reaped while its pod was cut off from Redis, or startup recovery of an id
 * that also reached its next list. Handlers must tolerate a second run (the
 * DB rows they write are fenced by attempt and unique keys, see JobRows).
 * What the queue rules out is loss: every move of an id between two Redis
 * keys is either one command (BRPOPLPUSH) or one Lua script (promote,
 * recover), or writes the destination before it clears the source (fail,
 * defer, dead_letter), so a crash or a Redis error leaves the id on at least
 * one list that a later pass, the reaper or startup recovery reads again.
 * While a handler runs, the worker renews its lease (renew_lease, driven by
 * Jobs::LeaseHeartbeat), so the visibility timeout bounds how long a dead
 * worker holds a job, not how long a live handler may take.
 */

#include "jobs/Jobs.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <utility>

#include <spdlog/spdlog.h>
#include <sw/redis++/redis++.h>

#include <nlohmann/json.hpp>

#include "cache/Cache.hpp"
#include "jobs/Dispatcher.hpp"
#include "observability/Trace.hpp"
#include "utils/Time.hpp"

namespace Jobs {

namespace {

// ZREM the id from the delayed set and LPUSH it onto its queue in one step:
// the old ZREM-then-LPUSH lost the id when the LPUSH failed. Only the caller
// whose ZREM removed the id pushes it, so concurrent promoters never push it
// twice. KEYS[1] delayed set, KEYS[2] queue; ARGV[1] id.
constexpr const char* kPromoteScript =
    "if redis.call('ZREM', KEYS[1], ARGV[1]) == 1 then "
    "redis.call('LPUSH', KEYS[2], ARGV[1]) return 1 end return 0";

// Move one id from a processing list back to its queue in one step.
// KEYS[1] processing list, KEYS[2] queue; ARGV[1] id.
constexpr const char* kRecoverScript =
    "if redis.call('LREM', KEYS[1], 0, ARGV[1]) > 0 then "
    "redis.call('LPUSH', KEYS[2], ARGV[1]) return 1 end return 0";

// Remove a lease only while it is still expired, so a lease renewed after
// the reaper's range read survives. KEYS[1] leases; ARGV[1] id, ARGV[2] now.
constexpr const char* kReapGateScript =
    "local s = redis.call('ZSCORE', KEYS[1], ARGV[1]) "
    "if s and tonumber(s) <= tonumber(ARGV[2]) then "
    "redis.call('ZREM', KEYS[1], ARGV[1]) return 1 end return 0";

// Renew a lease while the job is still this worker's: the blob says
// processing by ARGV[1] and the lease entry exists. KEYS[1] job blob,
// KEYS[2] leases; ARGV[1] worker id, ARGV[2] job id, ARGV[3] new deadline.
constexpr const char* kRenewLeaseScript =
    "local b = redis.call('GET', KEYS[1]) "
    "if not b then return 0 end "
    "local ok, j = pcall(cjson.decode, b) "
    "if not ok or j['status'] ~= 'processing' or j['worker_id'] ~= ARGV[1] then return 0 end "
    "if not redis.call('ZSCORE', KEYS[2], ARGV[2]) then return 0 end "
    "redis.call('ZADD', KEYS[2], ARGV[3], ARGV[2]) return 1";

}  // namespace

void JobQueue::initialize(long result_ttl, int max_retries) {
    if (initialized_) {
        throw std::runtime_error("JobQueue already initialized");
    }
    if (!Cache::is_initialized()) {
        throw std::runtime_error("Cache must be initialized before Jobs");
    }
    result_ttl_ = result_ttl;
    default_max_retries_ = max_retries;
    initialized_ = true;
    spdlog::info("Job queue initialized (result_ttl={}s, max_retries={})", result_ttl_, default_max_retries_);
}

void JobQueue::set_retry_backoff(int64_t base_ms, int64_t max_ms) {
    retry_backoff_base_ms_ = base_ms < 0 ? 0 : base_ms;
    retry_backoff_max_ms_ = max_ms < base_ms ? base_ms : max_ms;
}

void JobQueue::init_blocking_client(
    const std::string& host, int port, long brpop_timeout_sec, const std::string& password, int pool_size, int db) {
    const auto sock_to = std::chrono::milliseconds((brpop_timeout_sec + 2) * 1000);
    // One blocking connection is held per concurrent worker thread for the
    // duration of its BRPOP — size the pool to concurrency or threads beyond
    // the 4th stall waiting for a connection (silent throughput cliff).
    blocking_client_ = Cache::make_standalone_client(host, port, pool_size, password, sock_to, sock_to, db);
    blocking_client_->ping();
    spdlog::info("Jobs blocking Redis client initialized ({}:{}, db={}, socket_timeout={}s)",
                 host,
                 port,
                 db,
                 brpop_timeout_sec + 2);
}

void JobQueue::init_blocking_client_sentinel(const std::string& master_name,
                                             const std::vector<std::pair<std::string, int>>& sentinels,
                                             long brpop_timeout_sec,
                                             const std::string& password,
                                             const std::string& sentinel_password,
                                             int pool_size,
                                             int db) {
    const auto sock_to = std::chrono::milliseconds((brpop_timeout_sec + 2) * 1000);
    // Size to worker concurrency — see init_blocking_client.
    blocking_client_ = Cache::make_sentinel_client(
        master_name, sentinels, pool_size, password, sentinel_password, sock_to, sock_to, db);
    blocking_client_->ping();
    spdlog::info("Jobs blocking Redis client initialized via Sentinel (master: {}, db={}, socket_timeout={}s)",
                 master_name,
                 db,
                 brpop_timeout_sec + 2);
}

Job JobQueue::submit(const std::string& type, const json& payload, int max_retries) {
    check_initialized();
    auto& redis = Cache::get().get_client();

    Job job;
    job.id = generate_uuid();
    job.type = type;
    job.payload = payload;
    job.status = "pending";
    job.result = nullptr;
    job.retry_count = 0;
    job.max_retries = (max_retries >= 0) ? max_retries : default_max_retries_;
    job.created_at = now_epoch();
    job.updated_at = job.created_at;
    // Carry the originating request's trace context (if we're inside a
    // request) so the worker can continue the same distributed trace.
    job.traceparent = Observability::Trace::current_traceparent();

    // Store job data
    save_job_(redis, job);

    // Push job ID to the type-specific queue
    redis.lpush(queue_key(type), job.id);

    // Time-ordered indexes for paginated listing. Best-effort: a missed
    // ZADD only means the job won't show in the admin list.
    try {
        const double score = static_cast<double>(job.created_at);
        redis.zadd(index_key(), job.id, score);
        redis.zadd(index_key_for(type), job.id, score);
    } catch (const std::exception& e) {
        spdlog::warn("Jobs: failed to index job {}: {}", job.id, e.what());
    }

    spdlog::debug("Job submitted: id={} type={}", job.id, type);
    return job;
}

std::optional<Job> JobQueue::pick(const std::vector<std::string>& types,
                                  long timeout_sec,
                                  const std::string& worker_id) {
    check_initialized();
    // Local copy keeps the blocking client alive across the BRPOP even if
    // shutdown() resets the member concurrently (see member declaration).
    auto blocking = blocking_client_;
    auto& brpop_redis = blocking ? *blocking : Cache::get().get_client();
    auto& redis = Cache::get().get_client();

    const std::string proc_list = processing_key(worker_id);
    std::string job_id;

    if (types.empty()) {
        return std::nullopt;
    }
    if (types.size() == 1) {
        // BRPOPLPUSH is atomic: id moves from queue → processing list in
        // one round-trip. redis-plus-plus exposes this as brpoplpush.
        auto moved = brpop_redis.brpoplpush(queue_key(types[0]), proc_list, std::chrono::seconds(timeout_sec));
        if (!moved)
            return std::nullopt;
        job_id = *moved;
    } else {
        // Several types: one BRPOPLPUSH at a time, in rotation, so the move
        // stays atomic here too (see move_one_of_).
        auto moved = move_one_of_(brpop_redis, types, proc_list, timeout_sec);
        if (!moved)
            return std::nullopt;
        job_id = *moved;
    }

    // Fetch job data
    auto data = redis.get(job_key(job_id));
    if (!data) {
        spdlog::warn("Job {} popped from queue but data not found in Redis", job_id);
        // Clean up — ghost id shouldn't linger in processing list.
        try {
            redis.lrem(proc_list, 0, job_id);
        } catch (const std::exception& e) {
            spdlog::warn("Job {}: could not drop the ghost id from {}: {}", job_id, proc_list, e.what());
        }
        return std::nullopt;
    }

    auto job = Job::from_json(json::parse(*data));
    if (is_terminal_status_(job.status)) {
        // The id was on the queue after its blob reached a terminal state: a
        // cancel() that lost the race with this pop, or a completed job that
        // startup recovery pushed again. Running it would overwrite the
        // outcome; drop the queue entry instead.
        spdlog::info("Job {} popped from queue but already {} — skipped", job_id, job.status);
        try {
            redis.lrem(proc_list, 0, job_id);
        } catch (const std::exception& e) {
            spdlog::warn("Job {}: could not drop the spent id from {}: {}", job_id, proc_list, e.what());
        }
        return std::nullopt;
    }
    job.status = "processing";
    job.worker_id = worker_id;
    job.updated_at = now_epoch();

    // Update status in Redis
    save_job_(redis, job);

    // Lease the job so a cross-worker reaper can reclaim it if this worker
    // dies before complete()/fail(). Best-effort: a missed lease only loses
    // the visibility-timeout safety net, not the job (startup recovery still
    // covers a same-id restart).
    if (visibility_timeout_ms_ > 0) {
        try {
            redis.zadd(
                leases_key(), job.id, static_cast<double>(Utils::Time::now_epoch_millis() + visibility_timeout_ms_));
        } catch (const std::exception& e) {
            spdlog::warn("Job {}: could not record its lease: {}", job.id, e.what());
        }
    }

    spdlog::debug("Job picked: id={} type={} worker={}", job.id, job.type, worker_id);
    return job;
}

void JobQueue::complete(const std::string& id, const json& result) {
    check_initialized();
    auto& redis = Cache::get().get_client();

    auto job = load_job_or_throw_(redis, id);
    job.status = "completed";
    job.result = result;
    job.updated_at = now_epoch();

    redis.setex(job_key(id), result_ttl_, job.to_json().dump());
    // Drop from the worker's processing list — the job is officially done
    // and no recovery pass should resurrect it.
    release_from_worker_(redis, job);
    spdlog::debug("Job completed: id={}", id);
}

void JobQueue::fail(const std::string& id, const std::string& error) {
    check_initialized();
    auto& redis = Cache::get().get_client();

    auto job = load_job_or_throw_(redis, id);
    if (is_terminal_status_(job.status)) {
        // A lease left behind by complete() or cancel() (a pod killed between
        // the blob write and the ZREM) reaped later: the outcome stands, the
        // job must not run again.
        spdlog::info("Job {} fail({}) ignored: already {}", id, error, job.status);
        release_from_worker_(redis, job);
        return;
    }
    job.retry_count++;
    job.updated_at = now_epoch();

    // The id lands on its next list (live queue, delayed set or DLQ) BEFORE it
    // leaves the worker's processing list. The two writes are not atomic: a
    // crash in between then leaves the id on both lists, and startup recovery
    // pushes it once more (a duplicate, the documented at-least-once
    // contract). The other order left it on neither, with no lease to reap
    // and a blob stuck "pending" that nothing would ever find again.
    if (job.retry_count < job.max_retries) {
        // Requeue for retry.
        job.status = "pending";
        job.error = error;
        save_job_(redis, job);
        const int64_t delay_ms = backoff_delay_ms_(job.retry_count);
        if (delay_ms > 0) {
            // Park in the delayed set; promote_due_jobs() returns it to the
            // live queue once the backoff window elapses. Avoids a tight
            // pick→fail→pick loop when the queue is otherwise empty.
            redis.zadd(delayed_key(), id, static_cast<double>(Utils::Time::now_epoch_millis() + delay_ms));
            spdlog::info(
                "Job {} retry {}/{} scheduled in {}ms: {}", id, job.retry_count, job.max_retries, delay_ms, error);
        } else {
            redis.lpush(queue_key(job.type), id);
            spdlog::info("Job {} requeued (retry {}/{}): {}", id, job.retry_count, job.max_retries, error);
        }
        release_from_worker_(redis, job);
    } else {
        // Max retries exceeded — send to dead-letter queue for inspection.
        // DLQ entries are NOT reaped by result_ttl_ (operator must drain
        // or requeue them explicitly via /api/v1/jobs/dlq/{id}/requeue).
        job.status = "dead";
        job.error = error;
        save_job_(redis, job);
        redis.lpush(dlq_key(job.type), id);
        redis.sadd(kDlqAllKey, id);  // index for "list all DLQ" queries
        release_from_worker_(redis, job);
        spdlog::warn("Job {} DLQ'd after {} retries: {}", id, job.retry_count, error);
        Dispatcher::get().on_dead(job, error);
    }
}

void JobQueue::defer(const std::string& id, int64_t delay_ms, const std::string& reason) {
    check_initialized();
    auto& redis = Cache::get().get_client();
    auto job = load_job_or_throw_(redis, id);
    // retry_count stays: waiting for another delivery to let go is not an attempt.
    job.status = "pending";
    job.error = reason;
    const std::string worker_id = job.worker_id;
    job.worker_id.clear();
    job.updated_at = now_epoch();
    save_job_(redis, job);
    const int64_t wait = delay_ms < 1000 ? 1000 : delay_ms;
    // Delayed set first, processing list second: see fail().
    redis.zadd(delayed_key(), id, static_cast<double>(Utils::Time::now_epoch_millis() + wait));
    job.worker_id = worker_id;
    release_from_worker_(redis, job);
    spdlog::info("Job {} deferred for {}ms: {}", id, wait, reason);
}

void JobQueue::dead_letter(const std::string& id, const std::string& error) {
    check_initialized();
    auto& redis = Cache::get().get_client();
    auto job = load_job_or_throw_(redis, id);
    job.updated_at = now_epoch();
    job.status = "dead";
    job.error = error;
    // Index BEFORE committing the blob: the 3 writes aren't atomic, and if
    // lpush/sadd threw after the blob was already marked "dead" the job
    // would vanish from list_dlq/requeue/recover entirely. Indexing first
    // means a failure leaves it discoverable (with its prior status) and
    // list_dlq heals once the blob write lands. The processing list is
    // released last, after the id is on the DLQ: see fail().
    redis.lpush(dlq_key(job.type), id);
    redis.sadd(kDlqAllKey, id);
    save_job_(redis, job);
    release_from_worker_(redis, job);
    spdlog::warn("Job {} sent straight to DLQ (permanent failure): {}", id, error);
    Dispatcher::get().on_dead(job, error);
}

std::vector<Job> JobQueue::list_dlq(const std::string& type, int limit) {
    check_initialized();
    auto& redis = Cache::get().get_client();
    std::vector<std::string> ids;
    if (type.empty()) {
        // Scan the index set.
        redis.smembers(kDlqAllKey, std::back_inserter(ids));
    } else {
        redis.lrange(dlq_key(type), 0, limit - 1, std::back_inserter(ids));
    }
    if (static_cast<int>(ids.size()) > limit)
        ids.resize(limit);
    // One MGET instead of a GET per id (avoids N round-trips).
    return fetch_jobs_by_ids_(ids);
}

bool JobQueue::requeue_from_dlq(const std::string& id) {
    check_initialized();
    auto& redis = Cache::get().get_client();
    Job job;
    try {
        auto loaded = load_job_(redis, id);
        if (!loaded)
            return false;
        job = std::move(*loaded);
    } catch (const json::exception&) {
        // Unparseable blob — same "not requeueable" answer as a missing one.
        // Redis errors still propagate (see the class exception contract).
        return false;
    }
    if (job.status != "dead")
        return false;

    // Remove from per-type DLQ list + index.
    redis.lrem(dlq_key(job.type), 0, id);
    redis.srem(kDlqAllKey, id);

    job.status = "pending";
    job.retry_count = 0;
    job.error.clear();
    job.updated_at = now_epoch();
    save_job_(redis, job);
    redis.lpush(queue_key(job.type), id);
    spdlog::info("Job {} requeued from DLQ", id);
    return true;
}

long JobQueue::dlq_depth() {
    if (!initialized_)
        return 0;
    try {
        return static_cast<long>(Cache::get().get_client().scard(kDlqAllKey));
    } catch (const std::exception& e) {
        spdlog::warn("dlq_depth: {}", e.what());
        return 0;
    }
}

std::unordered_map<std::string, long> JobQueue::dlq_depth_by_type() {
    return depth_by_prefix_("jobs:dlq:", kDlqAllKey, "dlq_depth_by_type");
}

std::unordered_map<std::string, long> JobQueue::queue_depth_by_type() {
    return depth_by_prefix_("jobs:queue:", "", "queue_depth_by_type");
}

long JobQueue::recover_processing(const std::string& worker_id) {
    check_initialized();
    if (worker_id.empty())
        return 0;
    auto& redis = Cache::get().get_client();
    const std::string proc = processing_key(worker_id);
    long recovered = 0;
    // Snapshot the list and move each id back onto its type queue with one
    // script per id (LREM + LPUSH), during startup only: the worker has not
    // picked anything yet, so nothing else writes here. An id whose move
    // fails stays on the list for the next start; the old final DEL of the
    // whole list dropped it instead.
    std::vector<std::string> ids;
    try {
        redis.lrange(proc, 0, -1, std::back_inserter(ids));
    } catch (const std::exception& e) {
        spdlog::warn("Jobs: could not read {} for recovery: {}", proc, e.what());
        return 0;
    }
    for (const auto& id : ids) {
        try {
            std::optional<Job> loaded;
            try {
                loaded = load_job_(redis, id);
            } catch (const json::exception& e) {
                spdlog::warn("Jobs: dropping {} from {}: unreadable blob: {}", id, proc, e.what());
            }
            // A crash between complete()'s blob write and its lrem leaves a
            // finished job here; running it again would send a second email
            // or webhook. The same for a cancelled or dead-lettered one. A
            // missing or unreadable blob has nothing left to run.
            if (!loaded || is_terminal_status_(loaded->status)) {
                redis.lrem(proc, 0, id);
                continue;
            }
            Job job = std::move(*loaded);
            // Put it back on the live queue so another worker (or this one
            // after restart) picks it up again. Don't touch retry_count —
            // the job didn't fail, we just missed its completion.
            job.status = "pending";
            job.worker_id.clear();
            job.updated_at = now_epoch();
            save_job_(redis, job);
            if (redis.eval<long long>(kRecoverScript, {proc, queue_key(job.type)}, {id}) > 0)
                ++recovered;
        } catch (const std::exception& e) {
            spdlog::warn("Jobs: could not recover {} from {} (left for the next start): {}", id, proc, e.what());
        }
    }
    if (recovered > 0) {
        spdlog::warn("Jobs: recovered {} orphaned jobs from {}", recovered, proc);
    }
    return recovered;
}

long JobQueue::promote_due_jobs(int64_t now_ms) {
    // Not gated on retry backoff: defer() parks jobs in the same set.
    if (!initialized_)
        return 0;
    auto& redis = Cache::get().get_client();
    if (now_ms < 0)
        now_ms = Utils::Time::now_epoch_millis();
    std::vector<std::string> ids;
    // A read error propagates: the worker's maintenance pass logs it.
    redis.zrangebyscore(
        delayed_key(),
        sw::redis::BoundedInterval<double>(0.0, static_cast<double>(now_ms), sw::redis::BoundType::CLOSED),
        std::back_inserter(ids));
    long moved = 0;
    for (const auto& id : ids) {
        try {
            std::optional<Job> job;
            try {
                job = load_job_(redis, id);
            } catch (const json::exception& e) {
                spdlog::warn("Job {}: unreadable blob, dropped from the delayed set: {}", id, e.what());
            }
            if (!job || is_terminal_status_(job->status)) {
                // Blob expired, unreadable, or cancelled while it waited: the
                // entry is spent.
                redis.zrem(delayed_key(), id);
                continue;
            }
            // ZREM and LPUSH in one script: only the caller whose ZREM removed
            // the id pushes it, and no error can land between the two.
            if (redis.eval<long long>(kPromoteScript, {delayed_key(), queue_key(job->type)}, {id}) > 0)
                ++moved;
        } catch (const std::exception& e) {
            // The id stays in the delayed set; the next pass tries again.
            spdlog::warn("Job {}: promotion failed, retried next pass: {}", id, e.what());
        }
    }
    return moved;
}

long JobQueue::reap_expired_leases(int64_t now_ms) {
    if (!initialized_ || visibility_timeout_ms_ <= 0)
        return 0;
    auto& redis = Cache::get().get_client();
    if (now_ms < 0)
        now_ms = Utils::Time::now_epoch_millis();
    const std::string now_text = std::to_string(now_ms);
    std::vector<std::string> ids;
    // A read error propagates: the worker's maintenance pass logs it.
    redis.zrangebyscore(
        leases_key(),
        sw::redis::BoundedInterval<double>(0.0, static_cast<double>(now_ms), sw::redis::BoundType::CLOSED),
        std::back_inserter(ids));
    long reaped = 0;
    for (const auto& id : ids) {
        bool gated = false;
        try {
            // Exactly one reaper wins each expired lease, and a lease renewed
            // since the range read is not taken (the script checks the score).
            gated = redis.eval<long long>(kReapGateScript, {leases_key()}, {id, now_text}) > 0;
            if (!gated)
                continue;
            fail(id, "visibility timeout exceeded");
            ++reaped;
        } catch (const std::exception& e) {
            spdlog::warn("Job {}: reaping its expired lease failed: {}", id, e.what());
            if (!gated)
                continue;
            // The lease is gone but fail() did not finish: put it back as
            // expired so the next pass runs fail() again, instead of leaving
            // the job to startup recovery alone.
            try {
                redis.zadd(leases_key(), id, static_cast<double>(now_ms));
            } catch (const std::exception& inner) {
                spdlog::error("Job {}: could not restore its expired lease: {}", id, inner.what());
            }
        }
    }
    return reaped;
}

bool JobQueue::renew_lease(const std::string& id, const std::string& worker_id) {
    check_initialized();
    if (visibility_timeout_ms_ <= 0 || worker_id.empty())
        return false;
    auto& redis = Cache::get().get_client();
    const auto deadline = Utils::Time::now_epoch_millis() + visibility_timeout_ms_;
    return redis.eval<long long>(
               kRenewLeaseScript, {job_key(id), leases_key()}, {worker_id, id, std::to_string(deadline)}) > 0;
}

long JobQueue::trim_indexes(int64_t now_sec) {
    check_initialized();
    auto& redis = Cache::get().get_client();
    if (now_sec < 0)
        now_sec = now_epoch();
    // Scores are created_at in seconds; an entry is older than result_ttl
    // when its score is below now - result_ttl.
    const sw::redis::RightBoundedInterval<double> older(static_cast<double>(now_sec - result_ttl_),
                                                        sw::redis::BoundType::OPEN);
    long removed = static_cast<long>(redis.zremrangebyscore(index_key(), older));
    const std::string pattern = index_key_for("*");
    long long cursor = 0;
    do {
        std::vector<std::string> keys;
        cursor = redis.scan(cursor, pattern, 100, std::back_inserter(keys));
        for (const auto& key : keys)
            removed += static_cast<long>(redis.zremrangebyscore(key, older));
    } while (cursor != 0);
    if (removed > 0)
        spdlog::debug("Jobs: trimmed {} index entries older than {}s", removed, result_ttl_);
    return removed;
}

std::optional<Job> JobQueue::get_status(const std::string& id) {
    check_initialized();
    auto& redis = Cache::get().get_client();

    return load_job_(redis, id);
}

bool JobQueue::cancel(const std::string& id) {
    check_initialized();
    auto& redis = Cache::get().get_client();

    // Compare-and-set on the job blob: the write lands only if the blob is
    // still the one the status was decided on, so of two concurrent
    // cancellers (or a canceller racing a worker's complete/fail) exactly one
    // transition wins and the other re-reads the new state. The new blob is
    // built here rather than in Lua so its JSON keeps Job::to_json's shape.
    static constexpr const char* kCompareAndSet =
        "if redis.call('GET', KEYS[1]) == ARGV[1] then "
        "redis.call('SETEX', KEYS[1], ARGV[3], ARGV[2]) return 1 else return 0 end";
    constexpr int kAttempts = 8;
    const std::string key = job_key(id);
    const std::string ttl = std::to_string(result_ttl_);

    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        const auto raw = redis.get(key);
        if (!raw) {
            return false;
        }
        auto job = Job::from_json(json::parse(*raw));
        // Terminal states can't be cancelled. The real vocabulary is
        // pending/processing/completed/dead (+ "failed", which only cancel()
        // itself sets). The old guard checked "failed" but not "dead", so a DLQ
        // ("dead") job could be cancelled — that TTL-expired its blob while the
        // id stayed indexed in the DLQ set, leaving a dangling entry.
        if (job.status == "completed" || job.status == "dead" || job.status == "failed") {
            return false;
        }

        const bool was_processing = (job.status == "processing");
        job.status = "failed";
        job.error = "cancelled";
        job.updated_at = now_epoch();

        if (redis.eval<long long>(kCompareAndSet, {key}, {*raw, job.to_json().dump(), ttl}) == 0) {
            continue;  // the blob changed after the read: decide again on the new state
        }

        // Drop it from wherever it currently lives: the pending queue, the
        // delayed set (a retry waiting out its backoff, or a deferred job) or —
        // if a worker already claimed it — that worker's processing list, so
        // neither promote_due_jobs() nor crash recovery resurrects a cancelled
        // job.
        redis.lrem(queue_key(job.type), 0, id);
        redis.zrem(delayed_key(), id);
        if (was_processing && !job.worker_id.empty())
            redis.lrem(processing_key(job.worker_id), 0, id);
        clear_lease_(redis, id);

        spdlog::debug("Job cancelled: id={}", id);
        return true;
    }
    spdlog::warn("Job cancel gave up after {} contended attempts: id={}", kAttempts, id);
    return false;
}

JobQueue::JobPage JobQueue::list_paged(const std::string& type, int limit, int offset) {
    check_initialized();
    auto& redis = Cache::get().get_client();
    const std::string key = type.empty() ? index_key() : index_key_for(type);

    std::vector<std::string> ids;
    redis.zrevrange(key, offset, offset + limit - 1, std::back_inserter(ids));

    JobPage page;
    if (!ids.empty()) {
        std::vector<std::string> stale;
        page.jobs = fetch_jobs_by_ids_(ids, &stale);
        // Heal: blob expired → drop the id from the queried index (and,
        // when the type is known, from the global one too; global-query
        // staleness in typed indexes heals on their own queries).
        for (const auto& id : stale) {
            try {
                redis.zrem(key, id);
                if (!type.empty())
                    redis.zrem(index_key(), id);
            } catch (const std::exception& e) {
                spdlog::warn("Jobs: could not heal index entry {}: {}", id, e.what());
            }
        }
    }
    page.total = static_cast<long>(redis.zcard(key));
    return page;
}

void JobQueue::set_trace_id(const std::string& id, const std::string& trace_id) {
    check_initialized();
    auto& redis = Cache::get().get_client();
    std::optional<Job> job;
    try {
        job = load_job_(redis, id);
    } catch (const json::exception&) {
        return;  // unparseable blob — best-effort no-op
    }
    if (!job)
        return;
    try {
        job->trace_id = trace_id;
        job->updated_at = now_epoch();
        save_job_(redis, *job);
    } catch (const std::exception& e) {
        spdlog::debug("Job {}: set_trace_id failed: {}", id, e.what());
    }
}

bool JobQueue::health_check() {
    if (!initialized_)
        return false;
    try {
        Cache::get().get_client().ping();
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Jobs health check failed: {}", e.what());
        return false;
    }
}

void JobQueue::shutdown() {
    if (initialized_) {
        spdlog::info("Shutting down job queue");
        blocking_client_.reset();
        initialized_ = false;
    }
}

void JobQueue::check_initialized() const {
    if (!initialized_) {
        throw std::runtime_error("JobQueue not initialized");
    }
}

std::optional<std::string> JobQueue::move_one_of_(sw::redis::Redis& redis,
                                                  const std::vector<std::string>& types,
                                                  const std::string& proc_list,
                                                  long timeout_sec) {
    // Redis has no blocking pop over several keys that also pushes (BLMPOP on
    // 7.0+ does not, and the client's support for it is not pinned), and the
    // old BRPOP + RPUSH pair had a window in which a crash lost the id. So
    // the id always moves with one RPOPLPUSH / BRPOPLPUSH:
    //   1. a pass over every queue without blocking, starting at a per-call
    //      rotation, so a busy type cannot starve the others;
    //   2. a blocking wait of one slice on the pass's first type; the next
    //      pass starts one type later.
    // A job that lands on any queue during the wait is taken by the next pass,
    // so the added latency is at most one slice. The slice stays well under
    // the blocking client's socket timeout (brpop_timeout + 2 s, see
    // init_blocking_client), and the passes stop at the caller's timeout
    // (0 = wait forever, as BRPOP did).
    using clock = std::chrono::steady_clock;
    constexpr long kSliceSec = 1;
    const auto deadline = clock::now() + std::chrono::seconds(timeout_sec);
    std::size_t start = pick_rotation_.fetch_add(1, std::memory_order_relaxed) % types.size();
    for (;;) {
        for (std::size_t i = 0; i < types.size(); ++i) {
            const auto& type = types[(start + i) % types.size()];
            if (auto moved = redis.rpoplpush(queue_key(type), proc_list))
                return *moved;
        }
        long slice = kSliceSec;
        if (timeout_sec > 0) {
            const auto left = std::chrono::ceil<std::chrono::seconds>(deadline - clock::now()).count();
            if (left <= 0)
                return std::nullopt;
            slice = std::min<long>(slice, static_cast<long>(left));
        }
        if (auto moved = redis.brpoplpush(queue_key(types[start]), proc_list, std::chrono::seconds(slice)))
            return *moved;
        start = (start + 1) % types.size();
    }
}

void JobQueue::clear_lease_(sw::redis::Redis& redis, const std::string& id) {
    if (visibility_timeout_ms_ <= 0)
        return;
    try {
        redis.zrem(leases_key(), id);
    } catch (const std::exception& e) {
        // A lease left here is reaped after the visibility timeout; fail()
        // then sees the terminal status and leaves the job alone.
        spdlog::warn("Job {}: could not clear its lease: {}", id, e.what());
    }
}

std::optional<Job> JobQueue::load_job_(sw::redis::Redis& redis, const std::string& id) {
    auto data = redis.get(job_key(id));
    if (!data)
        return std::nullopt;
    return Job::from_json(json::parse(*data));
}

Job JobQueue::load_job_or_throw_(sw::redis::Redis& redis, const std::string& id) {
    auto job = load_job_(redis, id);
    if (!job)
        throw std::runtime_error("Job not found: " + id);
    return std::move(*job);
}

void JobQueue::save_job_(sw::redis::Redis& redis, const Job& job) {
    redis.set(job_key(job.id), job.to_json().dump());
}

void JobQueue::release_from_worker_(sw::redis::Redis& redis, const Job& job) {
    if (!job.worker_id.empty()) {
        try {
            redis.lrem(processing_key(job.worker_id), 0, job.id);
        } catch (const std::exception& e) {
            // Left on the list, the id is skipped by startup recovery when the
            // blob is terminal, or run again (at least once) when it is not.
            spdlog::warn("Job {}: could not leave {}: {}", job.id, processing_key(job.worker_id), e.what());
        }
    }
    clear_lease_(redis, job.id);
}

std::unordered_map<std::string, long> JobQueue::depth_by_prefix_(const std::string& prefix,
                                                                 const std::string& skip_key,
                                                                 const char* log_ctx) {
    std::unordered_map<std::string, long> out;
    if (!initialized_)
        return out;
    try {
        auto& redis = Cache::get().get_client();
        const std::string pattern = prefix + "*";
        long long cursor = 0;
        do {
            std::vector<std::string> batch;
            cursor = redis.scan(cursor, pattern, 100, std::back_inserter(batch));
            for (auto& k : batch) {
                if (!skip_key.empty() && k == skip_key)
                    continue;  // skip the `_all` index set
                long long len = redis.llen(k);
                std::string type = k.rfind(prefix, 0) == 0 ? k.substr(prefix.size()) : k;
                out[std::move(type)] = static_cast<long>(len);
            }
        } while (cursor != 0);
    } catch (const std::exception& e) {
        spdlog::warn("{}: scan failed: {}", log_ctx, e.what());
    }
    return out;
}

int64_t JobQueue::backoff_delay_ms_(int retry_count) const {
    if (retry_backoff_base_ms_ <= 0)
        return 0;
    int shift = retry_count > 0 ? retry_count - 1 : 0;
    if (shift > 30)
        shift = 30;  // guard the shift against overflow
    int64_t delay = retry_backoff_base_ms_ << shift;
    if (delay <= 0 || delay > retry_backoff_max_ms_)
        delay = retry_backoff_max_ms_;
    return delay;
}

std::vector<Job> JobQueue::fetch_jobs_by_ids_(const std::vector<std::string>& ids, std::vector<std::string>* stale) {
    std::vector<Job> jobs;
    if (ids.empty())
        return jobs;
    auto& redis = Cache::get().get_client();
    std::vector<std::string> keys;
    keys.reserve(ids.size());
    for (const auto& id : ids)
        keys.push_back(job_key(id));
    std::vector<sw::redis::OptionalString> vals;
    vals.reserve(keys.size());
    redis.mget(keys.begin(), keys.end(), std::back_inserter(vals));
    jobs.reserve(vals.size());
    for (size_t i = 0; i < vals.size(); ++i) {
        if (!vals[i]) {
            if (stale)
                stale->push_back(ids[i]);
            continue;
        }
        try {
            jobs.push_back(Job::from_json(json::parse(*vals[i])));
        } catch (...) {
            if (stale)
                stale->push_back(ids[i]);
        }
    }
    return jobs;
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

namespace {
/// Global job queue instance — module-private; reach it via the free
/// functions below (mirrors global_paypal_client in PayPalClient.cpp).
std::unique_ptr<JobQueue> global_jobs = nullptr;
}  // namespace

void initialize(long result_ttl, int max_retries) {
    if (global_jobs != nullptr) {
        throw std::runtime_error("JobQueue already initialized");
    }
    global_jobs = std::make_unique<JobQueue>();
    global_jobs->initialize(result_ttl, max_retries);
}

JobQueue& get() {
    if (global_jobs == nullptr) {
        throw std::runtime_error("JobQueue not initialized");
    }
    return *global_jobs;
}

bool is_initialized() {
    return global_jobs != nullptr && global_jobs->is_initialized();
}

void shutdown() {
    if (global_jobs) {
        global_jobs->shutdown();
        global_jobs.reset();
    }
}

}  // namespace Jobs
