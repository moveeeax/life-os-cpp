/**
 * @file test_jobs.cpp
 * @brief Integration tests for JobQueue: submit / pick / complete / fail-retry / cancel.
 * @details Requires live Redis. Tests cover the retry path and the cancel
 *          semantics (completed/failed jobs cannot be cancelled), including
 *          concurrent cancels of one job: exactly one of them wins; the
 *          worker's outcome mapping (Jobs::run_one), the lease heartbeat and
 *          the maintenance pass (jobs/Worker.hpp). Due times are checked with
 *          an explicit clock passed to promote_due_jobs / reap_expired_leases
 *          rather than with sleeps.
 */

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "jobs/Dispatcher.hpp"
#include "jobs/Jobs.hpp"
#include "jobs/Tick.hpp"
#include "jobs/Worker.hpp"
#include "test_helpers.hpp"
#include "utils/Time.hpp"

using json = nlohmann::json;

namespace {

class JobsIntegrationTest : public TestHelpers::CoreBackedTest {
protected:
    // Queue names used by the tests below; TearDown drains exactly these.
    static constexpr const char* kQueues[] = {"default",      "retryq",   "cancelq",    "pagedq",  "healq",
                                              "depthq",       "backoffq", "leaseq",     "deferq",  "hookq",
                                              "hookq-throws", "multiq-a", "multiq-b",   "runq",    "beatq",
                                              "trimq",        "promoteq", "cancelrunq", "orphanq", "tickq"};

    std::vector<std::string> job_ids_to_cleanup;

    std::string config_file_name() const override { return "jobs_int_test_config.json"; }

    void config_overrides(nlohmann::json& config) override {
        config["logging"]["name"] = "jobs_int_test";
        config["logging"]["file"] = "logs/jobs_int_test.log";
        config["observability"]["service_name"] = "jobs_int_svc";
        config["database"]["migrations_enabled"] = false;
        config["jobs"]["enabled"] = true;
        config["jobs"]["result_ttl"] = 3600;
        config["jobs"]["max_retries"] = 2;
    }

    // In-process tests don't go through worker_main, which is where
    // production normally calls init_blocking_client(). Without it,
    // pick()'s BRPOPLPUSH falls back to the Cache pool whose 500ms
    // socket_timeout is shorter than the 2s BRPOP argument and raises
    // "Resource temporarily unavailable" under load. Give the test a
    // dedicated blocking client so the timeout budgets match.
    void post_init() override {
        Jobs::get().init_blocking_client(
            TestHelpers::redis_host(), TestHelpers::redis_port(), /*brpop_timeout_sec=*/5, /*password=*/"");
    }

    void TearDown() override {
        if (Cache::is_initialized()) {
            for (const auto& id : job_ids_to_cleanup) {
                try {
                    Cache::get().del(Jobs::job_key(id));
                    Cache::get().get_client().zrem(Jobs::index_key(), id);
                } catch (...) {}
            }
            TestHelpers::drain_jobs({std::begin(kQueues), std::end(kQueues)});
            // The backoff / visibility-timeout ZSETs are global (not per-type),
            // so drain_jobs doesn't touch them — clear them here to keep tests
            // isolated.
            try {
                Cache::get().get_client().del(Jobs::delayed_key());
                Cache::get().get_client().del(Jobs::leases_key());
            } catch (...) {}
        }
        TestHelpers::CoreBackedTest::TearDown();
    }

    void track(const std::string& id) { job_ids_to_cleanup.push_back(id); }

    static std::vector<std::string> list(const std::string& key) {
        std::vector<std::string> out;
        Cache::get().get_client().lrange(key, 0, -1, std::back_inserter(out));
        return out;
    }

    static std::optional<double> lease_of(const std::string& id) {
        auto score = Cache::get().get_client().zscore(Jobs::leases_key(), id);
        if (!score)
            return std::nullopt;
        return *score;
    }

    // Submit one job of @p type and pick it as @p worker.
    Jobs::Job picked(const std::string& type, const std::string& worker) {
        auto job = Jobs::get().submit(type, {{"n", 1}});
        track(job.id);
        auto p = Jobs::get().pick({type}, 2, worker);
        if (!p || p->id != job.id)
            throw std::runtime_error("the job was not picked");
        return *p;
    }
};

TEST_F(JobsIntegrationTest, SubmitPickComplete) {
    auto job = Jobs::get().submit("default", {{"n", 1}});
    track(job.id);
    EXPECT_EQ(job.status, "pending");

    auto picked = Jobs::get().pick({"default"}, 2, "w1");
    ASSERT_TRUE(picked);
    EXPECT_EQ(picked->id, job.id);
    EXPECT_EQ(picked->status, "processing");
    EXPECT_EQ(picked->worker_id, "w1");

    Jobs::get().complete(job.id, {{"ok", true}});
    auto final_state = Jobs::get().get_status(job.id);
    ASSERT_TRUE(final_state);
    EXPECT_EQ(final_state->status, "completed");
    EXPECT_EQ(final_state->result.at("ok").get<bool>(), true);
}

TEST_F(JobsIntegrationTest, RecoverProcessingRequeuesLeftover) {
    auto job = Jobs::get().submit("recoverq", {{"n", 7}});
    track(job.id);
    // Claim onto a per-thread processing list (the worker-N-i id form from #15).
    auto picked = Jobs::get().pick({"recoverq"}, 2, "rwrk-0");
    ASSERT_TRUE(picked);
    ASSERT_EQ(picked->id, job.id);

    // Simulate a crash before completion: the id is stranded in rwrk-0's
    // processing list. recover_processing must push it back onto its queue —
    // the exact silent-job-loss path #15 fixed, previously untested.
    long recovered = Jobs::get().recover_processing("rwrk-0");
    EXPECT_EQ(recovered, 1);

    // Back on its queue and pickable again.
    auto again = Jobs::get().pick({"recoverq"}, 2, "rwrk-0");
    ASSERT_TRUE(again);
    EXPECT_EQ(again->id, job.id);
}

TEST_F(JobsIntegrationTest, FailRetriesThenPermanentFail) {
    // max_retries=2 from config
    auto job = Jobs::get().submit("retryq", {});
    track(job.id);

    // First attempt fails — should requeue.
    auto p1 = Jobs::get().pick({"retryq"}, 2, "w1");
    ASSERT_TRUE(p1);
    Jobs::get().fail(job.id, "boom-1");
    auto after1 = Jobs::get().get_status(job.id);
    ASSERT_TRUE(after1);
    EXPECT_EQ(after1->status, "pending");
    EXPECT_EQ(after1->retry_count, 1);

    // Second pick should re-pop the same job.
    auto p2 = Jobs::get().pick({"retryq"}, 2, "w2");
    ASSERT_TRUE(p2);
    EXPECT_EQ(p2->id, job.id);

    // Second failure reaches max_retries → moved to the dead-letter queue.
    Jobs::get().fail(job.id, "boom-2");
    auto after2 = Jobs::get().get_status(job.id);
    ASSERT_TRUE(after2);
    EXPECT_EQ(after2->status, "dead");
    EXPECT_EQ(after2->retry_count, 2);
    EXPECT_EQ(after2->error, "boom-2");

    // The job should be visible in the DLQ listing.
    auto dlq = Jobs::get().list_dlq("retryq", 10);
    bool found_in_dlq = false;
    for (const auto& j : dlq)
        if (j.id == job.id) {
            found_in_dlq = true;
            break;
        }
    EXPECT_TRUE(found_in_dlq);

    // Requeuing resets the state and pushes it back onto the live queue.
    EXPECT_TRUE(Jobs::get().requeue_from_dlq(job.id));
    auto after_requeue = Jobs::get().get_status(job.id);
    ASSERT_TRUE(after_requeue);
    EXPECT_EQ(after_requeue->status, "pending");
    EXPECT_EQ(after_requeue->retry_count, 0);
}

TEST_F(JobsIntegrationTest, CancelPendingJob) {
    auto job = Jobs::get().submit("cancelq", {});
    track(job.id);

    EXPECT_TRUE(Jobs::get().cancel(job.id));

    auto status = Jobs::get().get_status(job.id);
    ASSERT_TRUE(status);
    EXPECT_EQ(status->status, "failed");
    EXPECT_EQ(status->error, "cancelled");

    // Queue entry must have been removed so a worker doesn't pick a zombie.
    // Use LRANGE directly — BRPOP via pick() here would run against the Cache
    // pool's 100ms socket_timeout and raise a redis Error.
    std::vector<std::string> queue_contents;
    Cache::get().get_client().lrange(Jobs::queue_key("cancelq"), 0, -1, std::back_inserter(queue_contents));
    EXPECT_TRUE(queue_contents.empty());
}

// A cancel() that lands between a worker's pop and its own queue cleanup
// leaves the id on the queue with a "failed" blob: the pick must not turn it
// back into "processing" and run it.
TEST_F(JobsIntegrationTest, PickSkipsAJobCancelledAfterItWasQueued) {
    auto job = Jobs::get().submit("cancelq", {});
    track(job.id);
    ASSERT_TRUE(Jobs::get().cancel(job.id));
    Cache::get().get_client().lpush(Jobs::queue_key("cancelq"), job.id);

    EXPECT_FALSE(Jobs::get().pick({"cancelq"}, 1, "w1"));
    auto status = Jobs::get().get_status(job.id);
    ASSERT_TRUE(status);
    EXPECT_EQ(status->status, "failed");
    EXPECT_EQ(status->error, "cancelled");
    std::vector<std::string> proc;
    Cache::get().get_client().lrange(Jobs::processing_key("w1"), 0, -1, std::back_inserter(proc));
    EXPECT_TRUE(proc.empty()) << "the stale queue entry is dropped, not claimed";
}

// A crash between complete()'s blob write and its lrem leaves a finished job
// in the processing list; startup recovery must not run it a second time.
TEST_F(JobsIntegrationTest, RecoverProcessingSkipsACompletedJob) {
    auto job = Jobs::get().submit("recoverq", {});
    track(job.id);
    ASSERT_TRUE(Jobs::get().pick({"recoverq"}, 2, "rwrk-1"));
    Jobs::get().complete(job.id, {{"ok", true}});
    Cache::get().get_client().rpush(Jobs::processing_key("rwrk-1"), job.id);

    EXPECT_EQ(Jobs::get().recover_processing("rwrk-1"), 0);
    EXPECT_EQ(Jobs::get().get_status(job.id)->status, "completed");
    std::vector<std::string> q;
    Cache::get().get_client().lrange(Jobs::queue_key("recoverq"), 0, -1, std::back_inserter(q));
    EXPECT_TRUE(q.empty()) << "a completed job is not queued again";
}

// A job cancelled while it waited in the delayed set must not be promoted.
TEST_F(JobsIntegrationTest, PromoteSkipsACancelledJob) {
    auto job = Jobs::get().submit("cancelq", {});
    track(job.id);
    ASSERT_TRUE(Jobs::get().cancel(job.id));
    Cache::get().get_client().zadd(Jobs::delayed_key(), job.id, 1.0);

    EXPECT_EQ(Jobs::get().promote_due_jobs(), 0);
    std::vector<std::string> q;
    Cache::get().get_client().lrange(Jobs::queue_key("cancelq"), 0, -1, std::back_inserter(q));
    EXPECT_TRUE(q.empty());
    std::vector<std::string> delayed;
    Cache::get().get_client().zrange(Jobs::delayed_key(), 0, -1, std::back_inserter(delayed));
    EXPECT_TRUE(delayed.empty()) << "the spent entry is removed";
}

TEST_F(JobsIntegrationTest, CancelDropsADeferredJob) {
    auto job = Jobs::get().submit("cancelq", {});
    track(job.id);
    ASSERT_TRUE(Jobs::get().pick({"cancelq"}, 2, "w1"));
    Jobs::get().defer(job.id, /*delay_ms=*/1000, "the row is running elsewhere");

    EXPECT_TRUE(Jobs::get().cancel(job.id));
    EXPECT_EQ(Jobs::get().get_status(job.id)->status, "failed");

    // The delayed set must not hand the job back to the queue once its wait is over.
    std::vector<std::string> delayed;
    Cache::get().get_client().zrange(Jobs::delayed_key(), 0, -1, std::back_inserter(delayed));
    EXPECT_TRUE(delayed.empty()) << "a cancelled job left in jobs:delayed would be promoted and run";
    // Past the defer window, without sleeping through it.
    EXPECT_EQ(Jobs::get().promote_due_jobs(Utils::Time::now_epoch_millis() + 5000), 0);
    std::vector<std::string> queue_contents;
    Cache::get().get_client().lrange(Jobs::queue_key("cancelq"), 0, -1, std::back_inserter(queue_contents));
    EXPECT_TRUE(queue_contents.empty());
}

TEST_F(JobsIntegrationTest, CancelCompletedJobReturnsFalse) {
    auto job = Jobs::get().submit("cancelq", {});
    track(job.id);

    auto picked = Jobs::get().pick({"cancelq"}, 2, "w1");
    ASSERT_TRUE(picked);
    Jobs::get().complete(job.id, {{"ok", true}});

    EXPECT_FALSE(Jobs::get().cancel(job.id));

    auto status = Jobs::get().get_status(job.id);
    ASSERT_TRUE(status);
    EXPECT_EQ(status->status, "completed");
}

TEST_F(JobsIntegrationTest, CancelUnknownJobReturnsFalse) {
    EXPECT_FALSE(Jobs::get().cancel("00000000-0000-0000-0000-000000000000"));
}

// cancel() decides on the blob it read and writes with a compare-and-set, so
// concurrent cancellers of one pending job see exactly one success.
TEST_F(JobsIntegrationTest, CancelIsAtomicUnderContention) {
    auto job = Jobs::get().submit("cancelq", {});
    track(job.id);

    constexpr int kThreads = 4;
    std::atomic<int> successes{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            while (!go.load()) {
                std::this_thread::yield();
            }
            if (Jobs::get().cancel(job.id))
                ++successes;
        });
    }
    go.store(true);
    for (auto& t : threads)
        t.join();

    EXPECT_EQ(successes.load(), 1);
    auto status = Jobs::get().get_status(job.id);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->status, "failed");
    EXPECT_EQ(status->error, "cancelled");
}

TEST_F(JobsIntegrationTest, ListPagedSlicesWithExactTotal) {
    std::set<std::string> submitted;
    for (int i = 0; i < 5; ++i) {
        auto j = Jobs::get().submit("pagedq", {{"n", i}});
        track(j.id);
        submitted.insert(j.id);
    }

    auto p1 = Jobs::get().list_paged("pagedq", 2, 0);
    auto p2 = Jobs::get().list_paged("pagedq", 2, 2);
    auto p3 = Jobs::get().list_paged("pagedq", 2, 4);

    EXPECT_EQ(p1.total, 5);
    EXPECT_EQ(p1.jobs.size(), 2u);
    EXPECT_EQ(p2.jobs.size(), 2u);
    EXPECT_EQ(p3.jobs.size(), 1u);

    // The three pages partition the submitted set — no dup, no loss.
    // (Same-second submits share a score, so per-page order is tie-broken
    // lexicographically; only the partition property is guaranteed.)
    std::set<std::string> seen;
    for (const auto* page : {&p1, &p2, &p3})
        for (const auto& j : page->jobs)
            seen.insert(j.id);
    EXPECT_EQ(seen, submitted);
}

TEST_F(JobsIntegrationTest, ListPagedHealsExpiredIndexEntries) {
    auto j1 = Jobs::get().submit("healq", {{"k", 1}});
    auto j2 = Jobs::get().submit("healq", {{"k", 2}});
    track(j1.id);
    track(j2.id);

    // Simulate the blob expiring via result_ttl while the index entry stays.
    Cache::get().del(Jobs::job_key(j1.id));

    auto page = Jobs::get().list_paged("healq", 10, 0);
    ASSERT_EQ(page.jobs.size(), 1u);
    EXPECT_EQ(page.jobs[0].id, j2.id);

    // The stale id was healed out of the index: total is exact now.
    auto again = Jobs::get().list_paged("healq", 10, 0);
    EXPECT_EQ(again.total, 1);
}

TEST_F(JobsIntegrationTest, QueueDepthByTypeCountsWaiting) {
    // Measure our own delta rather than assume a clean slate.
    const auto before = Jobs::get().queue_depth_by_type();
    const long base = before.count("depthq") ? before.at("depthq") : 0;

    for (int i = 0; i < 3; ++i) {
        auto j = Jobs::get().submit("depthq", {{"n", i}});
        track(j.id);
    }

    auto depth = Jobs::get().queue_depth_by_type();
    ASSERT_TRUE(depth.count("depthq"));
    EXPECT_EQ(depth.at("depthq"), base + 3);

    // Picking a job off the waiting queue drops the depth by one.
    auto picked = Jobs::get().pick({"depthq"}, 2, "w1");
    ASSERT_TRUE(picked);
    auto after = Jobs::get().queue_depth_by_type();
    EXPECT_EQ(after.count("depthq") ? after.at("depthq") : 0, base + 2);
}

TEST_F(JobsIntegrationTest, SetTraceIdPersistsOnBlob) {
    auto j = Jobs::get().submit("default", {{"x", 1}});
    track(j.id);

    const std::string trace = "0123456789abcdef0123456789abcdef";
    Jobs::get().set_trace_id(j.id, trace);

    auto status = Jobs::get().get_status(j.id);
    ASSERT_TRUE(status);
    EXPECT_EQ(status->trace_id, trace);
}

TEST_F(JobsIntegrationTest, FailAfterCompleteLeavesTheJobDone) {
    // A lease left by complete() and reaped later calls fail(): the job must
    // stay completed and never return to the queue.
    auto job = Jobs::get().submit("donefailq", {});
    track(job.id);
    ASSERT_TRUE(Jobs::get().pick({"donefailq"}, 2, "w1"));
    Jobs::get().complete(job.id, json{{"ok", true}});
    Jobs::get().fail(job.id, "visibility timeout exceeded");

    auto after = Jobs::get().get_status(job.id);
    ASSERT_TRUE(after);
    EXPECT_EQ(after->status, "completed");
    EXPECT_EQ(after->retry_count, 0);
    std::vector<std::string> q;
    Cache::get().get_client().lrange(Jobs::queue_key("donefailq"), 0, -1, std::back_inserter(q));
    EXPECT_TRUE(q.empty());
}

// An admin's cancel that lands while the handler runs stands: the success or
// failure the worker records afterwards does not write over it (compare-and-set).
TEST_F(JobsIntegrationTest, ACancelWhileTheHandlerRunsStands) {
    auto done = picked("cancelrunq", "cw-0");
    ASSERT_TRUE(Jobs::get().cancel(done.id));
    Jobs::get().complete(done.id, json{{"ok", true}});
    EXPECT_EQ(Jobs::get().get_status(done.id)->status, "failed");
    EXPECT_EQ(Jobs::get().get_status(done.id)->error, "cancelled");

    auto failed = picked("cancelrunq", "cw-0");
    ASSERT_TRUE(Jobs::get().cancel(failed.id));
    Jobs::get().fail(failed.id, "boom");
    const auto after = Jobs::get().get_status(failed.id);
    EXPECT_EQ(after->status, "failed");
    EXPECT_EQ(after->error, "cancelled");
    EXPECT_EQ(after->retry_count, 0);
    EXPECT_TRUE(list(Jobs::queue_key("cancelrunq")).empty()) << "a cancelled job is not requeued";
    EXPECT_TRUE(list(Jobs::processing_key("cw-0")).empty());
}

// An id in another pod's processing list without a lease (the lease write
// failed, or the pod died between the pop and the lease) is requeued once its
// blob has not moved for a visibility timeout; a fresh one is left alone.
TEST_F(JobsIntegrationTest, AnOrphanWithoutALeaseIsRequeued) {
    Jobs::get().set_visibility_timeout(/*sec=*/100);
    auto job = picked("orphanq", "orphan-pod-0");
    Cache::get().get_client().zrem(Jobs::leases_key(), job.id);
    const int64_t now = Utils::Time::now_epoch_millis();

    EXPECT_EQ(Jobs::get().recover_orphans(now, "orphan-*"), 0) << "just picked: the lease may be on its way";
    EXPECT_EQ(list(Jobs::processing_key("orphan-pod-0")), std::vector<std::string>{job.id});

    EXPECT_EQ(Jobs::get().recover_orphans(now + 200 * 1000, "orphan-*"), 1);
    EXPECT_TRUE(list(Jobs::processing_key("orphan-pod-0")).empty());
    EXPECT_EQ(list(Jobs::queue_key("orphanq")), std::vector<std::string>{job.id});
    EXPECT_EQ(Jobs::get().get_status(job.id)->status, "pending");

    // A leased id belongs to the reaper (its own queue: the requeued orphan waits on orphanq).
    auto leased = picked("orphanq-leased", "orphan-pod-1");
    EXPECT_EQ(Jobs::get().recover_orphans(now + 200 * 1000, "orphan-*"), 0);
    EXPECT_EQ(list(Jobs::processing_key("orphan-pod-1")), std::vector<std::string>{leased.id});
    Jobs::get().complete(leased.id, json::object());
    Jobs::get().set_visibility_timeout(0);
}

// A minute's tick queues its job only when the last one ended.
TEST_F(JobsIntegrationTest, ATickQueuesOneJobAtATime) {
    Cache::get().del(Jobs::Tick::marker_key("tickq"));
    EXPECT_TRUE(Jobs::Tick::submit("tickq", json::object(), 600));
    EXPECT_FALSE(Jobs::Tick::submit("tickq", json::object(), 600)) << "the last tick's job still waits";
    EXPECT_EQ(list(Jobs::queue_key("tickq")).size(), 1u);
    Jobs::Tick::done("tickq");
    EXPECT_TRUE(Jobs::Tick::submit("tickq", json::object(), 600));
    EXPECT_EQ(list(Jobs::queue_key("tickq")).size(), 2u);
    Cache::get().del(Jobs::Tick::marker_key("tickq"));
}

TEST_F(JobsIntegrationTest, BackoffDelaysRequeueUntilDue) {
    Jobs::get().set_retry_backoff(/*base_ms=*/200, /*max_ms=*/1000);

    auto job = Jobs::get().submit("backoffq", {});
    track(job.id);

    auto p1 = Jobs::get().pick({"backoffq"}, 2, "w1");
    ASSERT_TRUE(p1);
    // The job is due 200 ms after fail(), so it is not due at any time up to
    // this moment, however slow the runner is.
    const int64_t before_fail = Utils::Time::now_epoch_millis();
    Jobs::get().fail(job.id, "boom");

    // Retry was counted and the job stays "pending", but with backoff enabled it
    // is parked in the delayed set, NOT immediately back on the live queue.
    auto after = Jobs::get().get_status(job.id);
    ASSERT_TRUE(after);
    EXPECT_EQ(after->status, "pending");
    EXPECT_EQ(after->retry_count, 1);

    std::vector<std::string> q;
    Cache::get().get_client().lrange(Jobs::queue_key("backoffq"), 0, -1, std::back_inserter(q));
    EXPECT_TRUE(q.empty());

    // Before the delay elapses, promotion moves nothing.
    EXPECT_EQ(Jobs::get().promote_due_jobs(before_fail), 0);

    // After the backoff window (at most max_ms = 1 s), the job is promoted
    // back and pickable again.
    EXPECT_EQ(Jobs::get().promote_due_jobs(Utils::Time::now_epoch_millis() + 5000), 1);

    auto p2 = Jobs::get().pick({"backoffq"}, 2, "w1");
    ASSERT_TRUE(p2);
    EXPECT_EQ(p2->id, job.id);
}

TEST_F(JobsIntegrationTest, DeferParksTheJobWithoutCountingAnAttempt) {
    // Retry backoff is off in this suite: a deferred job still waits in the delayed set.
    auto job = Jobs::get().submit("deferq", {});
    track(job.id);
    auto p1 = Jobs::get().pick({"deferq"}, 2, "dwrk-0");
    ASSERT_TRUE(p1);

    Jobs::get().defer(job.id, /*delay_ms=*/1000, "the row is running elsewhere");
    auto after = Jobs::get().get_status(job.id);
    ASSERT_TRUE(after);
    EXPECT_EQ(after->status, "pending");
    EXPECT_EQ(after->retry_count, 0) << "a wait is not an attempt";
    EXPECT_EQ(after->error, "the row is running elsewhere");

    std::vector<std::string> q;
    Cache::get().get_client().lrange(Jobs::queue_key("deferq"), 0, -1, std::back_inserter(q));
    EXPECT_TRUE(q.empty()) << "not back on the queue before its time";
    std::vector<std::string> proc;
    Cache::get().get_client().lrange(Jobs::processing_key("dwrk-0"), 0, -1, std::back_inserter(proc));
    EXPECT_TRUE(proc.empty()) << "a restart of the worker must not run it early";
    EXPECT_EQ(Jobs::get().promote_due_jobs(), 0);

    EXPECT_EQ(Jobs::get().promote_due_jobs(Utils::Time::now_epoch_millis() + 5000), 1)
        << "promoted with retry backoff off too";
    auto p2 = Jobs::get().pick({"deferq"}, 2, "dwrk-0");
    ASSERT_TRUE(p2);
    EXPECT_EQ(p2->id, job.id);
    EXPECT_EQ(p2->retry_count, 0);
}

TEST_F(JobsIntegrationTest, TheDeadLetterHookSeesTheJobTheQueueGaveUp) {
    std::vector<std::pair<json, std::string>> seen;
    Jobs::Dispatcher::get().register_dead_letter(
        "hookq", [&seen](const json& payload, const std::string& error) { seen.emplace_back(payload, error); });
    Jobs::Dispatcher::get().register_dead_letter(
        "hookq-throws", [](const json&, const std::string&) { throw std::runtime_error("the database is away"); });

    // Through the retry budget (max_retries = 2 in this suite).
    auto job = Jobs::get().submit("hookq", {{"job_id", "row-1"}});
    track(job.id);
    for (const char* error : {"boom-1", "boom-2"}) {
        ASSERT_TRUE(Jobs::get().pick({"hookq"}, 2, "hwrk-0"));
        Jobs::get().fail(job.id, error);
    }
    ASSERT_EQ(seen.size(), 1u) << "only the give-up, not the retry";
    EXPECT_EQ(seen[0].first["job_id"], "row-1");
    EXPECT_EQ(seen[0].second, "boom-2");

    // Straight to the DLQ.
    auto permanent = Jobs::get().submit("hookq", {{"job_id", "row-2"}});
    track(permanent.id);
    ASSERT_TRUE(Jobs::get().pick({"hookq"}, 2, "hwrk-0"));
    Jobs::get().dead_letter(permanent.id, "no handler");
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[1].first["job_id"], "row-2");

    // A hook that throws leaves the job dead-lettered and fail() quiet.
    auto other = Jobs::get().submit("hookq-throws", {}, /*max_retries=*/1);
    track(other.id);
    ASSERT_TRUE(Jobs::get().pick({"hookq-throws"}, 2, "hwrk-0"));
    EXPECT_NO_THROW(Jobs::get().fail(other.id, "boom"));
    EXPECT_EQ(Jobs::get().get_status(other.id)->status, "dead");

    Jobs::Dispatcher::get().register_dead_letter("hookq", {});
    Jobs::Dispatcher::get().register_dead_letter("hookq-throws", {});
}

TEST_F(JobsIntegrationTest, VisibilityTimeoutReapsExpiredLease) {
    Jobs::get().set_visibility_timeout(/*sec=*/100);  // pick() now records a lease

    auto job = Jobs::get().submit("leaseq", {});
    track(job.id);

    auto p1 = Jobs::get().pick({"leaseq"}, 2, "wkr-0");
    ASSERT_TRUE(p1);

    // Simulate a worker that picked the job and died without completing it:
    // backdate its lease so it's already expired.
    Cache::get().get_client().zadd(Jobs::leases_key(), job.id, 1.0);

    // The reaper reclaims the orphaned job: treated as a failure (retry bumped),
    // dropped from the dead worker's processing list, and requeued.
    EXPECT_EQ(Jobs::get().reap_expired_leases(), 1);

    auto after = Jobs::get().get_status(job.id);
    ASSERT_TRUE(after);
    EXPECT_EQ(after->retry_count, 1);

    std::vector<std::string> proc;
    Cache::get().get_client().lrange(Jobs::processing_key("wkr-0"), 0, -1, std::back_inserter(proc));
    EXPECT_TRUE(proc.empty());

    // Backoff is disabled in this test → fail() requeues immediately, so another
    // worker can pick it up.
    auto p2 = Jobs::get().pick({"leaseq"}, 2, "wkr-1");
    ASSERT_TRUE(p2);
    EXPECT_EQ(p2->id, job.id);
}

// Several types: the id moves from whichever queue holds it straight into the
// worker's processing list, one command, no window for a crash to lose it.
TEST_F(JobsIntegrationTest, MultiTypePickTakesTheSecondTypeWhenTheFirstQueueIsEmpty) {
    auto job = Jobs::get().submit("multiq-b", {{"n", 2}});
    track(job.id);

    auto picked = Jobs::get().pick({"multiq-a", "multiq-b"}, 2, "mw-0");
    ASSERT_TRUE(picked);
    EXPECT_EQ(picked->id, job.id);
    EXPECT_EQ(picked->type, "multiq-b");
    EXPECT_EQ(picked->status, "processing");
    EXPECT_EQ(picked->worker_id, "mw-0");

    // Right after the pick the id is on the processing list and nowhere else.
    std::vector<std::string> proc;
    Cache::get().get_client().lrange(Jobs::processing_key("mw-0"), 0, -1, std::back_inserter(proc));
    EXPECT_EQ(proc, std::vector<std::string>{job.id});
    std::vector<std::string> queue;
    Cache::get().get_client().lrange(Jobs::queue_key("multiq-b"), 0, -1, std::back_inserter(queue));
    EXPECT_TRUE(queue.empty());
    Jobs::get().complete(job.id, {{"ok", true}});

    // Two empty queues: the pick waits out its timeout and returns nothing.
    EXPECT_FALSE(Jobs::get().pick({"multiq-a", "multiq-b"}, 1, "mw-0"));
}

// The start of the round rotates per call: with both queues busy, two picks
// take one job of each type, so the first type listed cannot starve the other.
TEST_F(JobsIntegrationTest, MultiTypePickRotatesBetweenBusyTypes) {
    auto a = Jobs::get().submit("multiq-a", {{"n", 1}});
    track(a.id);
    auto b = Jobs::get().submit("multiq-b", {{"n", 2}});
    track(b.id);

    auto first = Jobs::get().pick({"multiq-a", "multiq-b"}, 2, "mw-1");
    auto second = Jobs::get().pick({"multiq-a", "multiq-b"}, 2, "mw-1");
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    const std::set<std::string> types{first->type, second->type};
    EXPECT_EQ(types, (std::set<std::string>{"multiq-a", "multiq-b"}));
    Jobs::get().complete(first->id, {{"ok", true}});
    Jobs::get().complete(second->id, {{"ok", true}});
}

TEST_F(JobsIntegrationTest, CompleteClearsVisibilityLease) {
    Jobs::get().set_visibility_timeout(/*sec=*/100);

    auto job = Jobs::get().submit("leaseq", {});
    track(job.id);

    auto p1 = Jobs::get().pick({"leaseq"}, 2, "wkr-0");
    ASSERT_TRUE(p1);
    // Lease recorded at pick. (redis-plus-plus Optional has operator bool, not
    // std::optional's has_value().)
    EXPECT_TRUE(static_cast<bool>(Cache::get().get_client().zscore(Jobs::leases_key(), job.id)));

    Jobs::get().complete(job.id, {{"ok", true}});

    // Completing the job clears its lease so the reaper can't resurrect it.
    EXPECT_FALSE(static_cast<bool>(Cache::get().get_client().zscore(Jobs::leases_key(), job.id)));
    EXPECT_EQ(Jobs::get().reap_expired_leases(), 0);
}

// ---- the worker's outcome mapping (Jobs::run_one) --------------------------

TEST_F(JobsIntegrationTest, RunOneCompletesAJobWhoseHandlerReturns) {
    const auto job = picked("runq", "run-0");
    int calls = 0;
    const auto run = Jobs::run_one(Jobs::get(), job, [&](const Jobs::Job&) {
        ++calls;
        return json{{"ok", true}};
    });
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(run.outcome, Jobs::Outcome::success);
    EXPECT_TRUE(run.recorded);
    EXPECT_STREQ(Jobs::outcome_label(run.outcome), "success");
    const auto after = Jobs::get().get_status(job.id);
    ASSERT_TRUE(after);
    EXPECT_EQ(after->status, "completed");
    EXPECT_EQ(after->result.at("ok").get<bool>(), true);
    EXPECT_TRUE(list(Jobs::processing_key("run-0")).empty());
}

// The bug run_one exists to rule out: complete() inside the handler's try sent
// a Redis error while recording a success to fail(), which ran the job again.
TEST_F(JobsIntegrationTest, RunOneNeverFailsAJobWhoseCompleteThrew) {
    const auto job = picked("runq", "run-1");
    const auto run = Jobs::run_one(Jobs::get(), job, [](const Jobs::Job& j) {
        // complete() now throws ("Job not found"), and so would fail().
        Cache::get().del(Jobs::job_key(j.id));
        return json{{"sent", 1}};
    });
    EXPECT_EQ(run.outcome, Jobs::Outcome::success) << "a success stays a success";
    EXPECT_FALSE(run.recorded);
    EXPECT_TRUE(run.error.empty());
    EXPECT_TRUE(list(Jobs::queue_key("runq")).empty()) << "not requeued";
    EXPECT_TRUE(list(Jobs::dlq_key("runq")).empty()) << "not dead-lettered";
    EXPECT_EQ(list(Jobs::processing_key("run-1")), std::vector<std::string>{job.id})
        << "left for the reaper or startup recovery, the at-least-once path";
    Cache::get().get_client().del(Jobs::processing_key("run-1"));
}

TEST_F(JobsIntegrationTest, RunOneDefersOnDeferJobWithoutCountingAnAttempt) {
    const auto job = picked("runq", "run-2");
    const auto run = Jobs::run_one(
        Jobs::get(), job, [](const Jobs::Job&) -> json { throw Jobs::DeferJob("the row is held", 1500); });
    EXPECT_EQ(run.outcome, Jobs::Outcome::deferred);
    EXPECT_TRUE(run.recorded);
    EXPECT_EQ(run.defer_ms, 1500);
    const auto after = Jobs::get().get_status(job.id);
    ASSERT_TRUE(after);
    EXPECT_EQ(after->status, "pending");
    EXPECT_EQ(after->retry_count, 0);
    EXPECT_TRUE(static_cast<bool>(Cache::get().get_client().zscore(Jobs::delayed_key(), job.id)));
    EXPECT_TRUE(list(Jobs::queue_key("runq")).empty());
}

TEST_F(JobsIntegrationTest, RunOneDeadLettersAPermanentErrorWithoutRetries) {
    const auto job = picked("runq", "run-3");
    const auto run =
        Jobs::run_one(Jobs::get(), job, [](const Jobs::Job&) -> json { throw Jobs::PermanentJobError("no handler"); });
    EXPECT_EQ(run.outcome, Jobs::Outcome::permanent_failure);
    EXPECT_EQ(run.error, "no handler");
    const auto after = Jobs::get().get_status(job.id);
    ASSERT_TRUE(after);
    EXPECT_EQ(after->status, "dead");
    EXPECT_EQ(after->retry_count, 0) << "the retry budget is not spent";
    EXPECT_EQ(list(Jobs::dlq_key("runq")), std::vector<std::string>{job.id});
}

TEST_F(JobsIntegrationTest, RunOneFailsAJobWhoseHandlerThrows) {
    const auto job = picked("runq", "run-4");
    const auto run =
        Jobs::run_one(Jobs::get(), job, [](const Jobs::Job&) -> json { throw std::runtime_error("boom"); });
    EXPECT_EQ(run.outcome, Jobs::Outcome::failure);
    EXPECT_EQ(run.error, "boom");
    const auto after = Jobs::get().get_status(job.id);
    ASSERT_TRUE(after);
    EXPECT_EQ(after->status, "pending");
    EXPECT_EQ(after->retry_count, 1);
    EXPECT_EQ(list(Jobs::queue_key("runq")), std::vector<std::string>{job.id}) << "backoff is off here";
}

TEST_F(JobsIntegrationTest, RunOneFailsAJobWhoseHandlerThrowsANonStdType) {
    const auto job = picked("runq", "run-5");
    const auto run = Jobs::run_one(Jobs::get(), job, [](const Jobs::Job&) -> json { throw 42; });
    EXPECT_EQ(run.outcome, Jobs::Outcome::failure);
    EXPECT_EQ(run.error, "non-std exception in handler");
    const auto after = Jobs::get().get_status(job.id);
    ASSERT_TRUE(after);
    EXPECT_EQ(after->retry_count, 1);
    EXPECT_EQ(after->error, "non-std exception in handler");
}

// ---- the lease heartbeat -------------------------------------------------

// A handler that outlives the visibility timeout keeps its lease: the
// heartbeat moves it ahead, and the reaper, at a time past the first lease,
// leaves the job alone.
TEST_F(JobsIntegrationTest, RunOneRenewsTheLeaseWhileTheHandlerRuns) {
    Jobs::get().set_visibility_timeout(/*sec=*/1);  // renewed every ~333 ms
    const auto job = picked("beatq", "beat-0");
    const auto first = lease_of(job.id);
    ASSERT_TRUE(first);

    long reaped = -1;
    const auto run = Jobs::run_one(Jobs::get(), job, [&](const Jobs::Job& j) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            const auto now = lease_of(j.id);
            if (now && *now > *first)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        // Past the lease pick() wrote, before the renewed one.
        reaped = Jobs::get().reap_expired_leases(static_cast<int64_t>(*first) + 1);
        return json{{"ok", true}};
    });
    EXPECT_EQ(reaped, 0) << "a renewed lease is not reaped";
    EXPECT_EQ(run.outcome, Jobs::Outcome::success);
    const auto after = Jobs::get().get_status(job.id);
    ASSERT_TRUE(after);
    EXPECT_EQ(after->status, "completed");
    EXPECT_EQ(after->retry_count, 0);
    EXPECT_FALSE(lease_of(job.id)) << "the heartbeat stopped before complete() cleared the lease";
}

// The heartbeat never extends a job that is no longer this worker's: one the
// reaper took back and another worker picked, or one whose lease is gone.
TEST_F(JobsIntegrationTest, RenewLeaseLeavesAJobAnotherWorkerTookOver) {
    Jobs::get().set_visibility_timeout(/*sec=*/100);
    const auto job = picked("beatq", "beat-1");
    EXPECT_TRUE(Jobs::get().renew_lease(job.id, "beat-1"));

    // The reaper takes it back, another worker picks it.
    Cache::get().get_client().zadd(Jobs::leases_key(), job.id, 1.0);
    ASSERT_EQ(Jobs::get().reap_expired_leases(), 1);
    auto again = Jobs::get().pick({"beatq"}, 2, "beat-2");
    ASSERT_TRUE(again);
    const auto theirs = lease_of(job.id);
    ASSERT_TRUE(theirs);

    EXPECT_FALSE(Jobs::get().renew_lease(job.id, "beat-1"));
    EXPECT_EQ(lease_of(job.id), theirs) << "the new owner's lease is untouched";
    EXPECT_TRUE(Jobs::get().renew_lease(job.id, "beat-2"));

    // A lease that is gone is not recreated.
    Cache::get().get_client().zrem(Jobs::leases_key(), job.id);
    EXPECT_FALSE(Jobs::get().renew_lease(job.id, "beat-2"));
    EXPECT_FALSE(lease_of(job.id));
}

TEST_F(JobsIntegrationTest, AStoppedHeartbeatRenewsNothing) {
    Jobs::get().set_visibility_timeout(/*sec=*/100);
    const auto job = picked("beatq", "beat-3");
    Jobs::LeaseHeartbeat beat(Jobs::get(), job.id, "beat-3", std::chrono::milliseconds(100));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (beat.renewals() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT_GT(beat.renewals(), 0);
    beat.stop();
    const int renewals = beat.renewals();
    const auto lease = lease_of(job.id);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(beat.renewals(), renewals);
    EXPECT_EQ(lease_of(job.id), lease);
}

TEST_F(JobsIntegrationTest, AHeartbeatOfAnotherWorkerRenewsNothing) {
    Jobs::get().set_visibility_timeout(/*sec=*/100);
    const auto job = picked("beatq", "beat-4");
    const auto lease = lease_of(job.id);
    Jobs::LeaseHeartbeat beat(Jobs::get(), job.id, "someone-else", std::chrono::milliseconds(100));
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    EXPECT_EQ(beat.renewals(), 0);
    EXPECT_EQ(lease_of(job.id), lease) << "beat-4's lease is untouched";
}

// ---- promote, recover and the maintenance pass ---------------------------

TEST_F(JobsIntegrationTest, PromoteMovesADueJobOnceAndDropsAnExpiredBlob) {
    auto job = Jobs::get().submit("promoteq", {});
    track(job.id);
    Cache::get().get_client().del(Jobs::queue_key("promoteq"));
    Cache::get().get_client().zadd(Jobs::delayed_key(), job.id, 1.0);
    Cache::get().get_client().zadd(Jobs::delayed_key(), "no-such-job", 1.0);

    EXPECT_EQ(Jobs::get().promote_due_jobs(), 1);
    EXPECT_EQ(Jobs::get().promote_due_jobs(), 0) << "a second pass finds nothing";
    EXPECT_EQ(list(Jobs::queue_key("promoteq")), std::vector<std::string>{job.id});
    std::vector<std::string> delayed;
    Cache::get().get_client().zrange(Jobs::delayed_key(), 0, -1, std::back_inserter(delayed));
    EXPECT_TRUE(delayed.empty()) << "the entry of a missing blob is spent";
}

TEST_F(JobsIntegrationTest, RecoverRequeuesTheLiveIdAndDropsASpentOne) {
    auto live = Jobs::get().submit("promoteq", {});
    track(live.id);
    ASSERT_TRUE(Jobs::get().pick({"promoteq"}, 2, "rec-0"));
    Cache::get().get_client().rpush(Jobs::processing_key("rec-0"), "no-such-job");

    EXPECT_EQ(Jobs::get().recover_processing("rec-0"), 1);
    EXPECT_TRUE(list(Jobs::processing_key("rec-0")).empty());
    EXPECT_EQ(list(Jobs::queue_key("promoteq")), std::vector<std::string>{live.id});
    EXPECT_EQ(Jobs::get().get_status(live.id)->status, "pending");
}

TEST_F(JobsIntegrationTest, TrimIndexesDropsEntriesOlderThanTheResultTtl) {
    auto old_job = Jobs::get().submit("trimq", {});
    auto new_job = Jobs::get().submit("trimq", {});
    track(old_job.id);
    track(new_job.id);
    // Created long before result_ttl (3600 s in this suite).
    const double long_ago = static_cast<double>(Jobs::now_epoch() - 10 * 3600);
    Cache::get().get_client().zadd(Jobs::index_key(), old_job.id, long_ago);
    Cache::get().get_client().zadd(Jobs::index_key_for("trimq"), old_job.id, long_ago);

    EXPECT_GE(Jobs::get().trim_indexes(), 2);
    auto& redis = Cache::get().get_client();
    EXPECT_FALSE(static_cast<bool>(redis.zscore(Jobs::index_key(), old_job.id)));
    EXPECT_FALSE(static_cast<bool>(redis.zscore(Jobs::index_key_for("trimq"), old_job.id)));
    EXPECT_TRUE(static_cast<bool>(redis.zscore(Jobs::index_key(), new_job.id)));
    EXPECT_TRUE(static_cast<bool>(redis.zscore(Jobs::index_key_for("trimq"), new_job.id)));
}

TEST_F(JobsIntegrationTest, MaintenanceTrimsOncePerIntervalAndPromotesEveryPass) {
    Jobs::Maintenance maintenance(std::chrono::seconds(600));
    const auto t0 = std::chrono::steady_clock::now();
    auto job = Jobs::get().submit("trimq", {});
    track(job.id);
    Cache::get().get_client().zadd(
        Jobs::index_key_for("trimq"), job.id, static_cast<double>(Jobs::now_epoch() - 10 * 3600));
    Cache::get().get_client().del(Jobs::queue_key("trimq"));
    Cache::get().get_client().zadd(Jobs::delayed_key(), job.id, 1.0);

    const auto first = maintenance.run(Jobs::get(), t0);
    EXPECT_EQ(first.errors, 0);
    EXPECT_EQ(first.promoted, 1);
    EXPECT_GE(first.trimmed, 1);

    Cache::get().get_client().zadd(
        Jobs::index_key_for("trimq"), job.id, static_cast<double>(Jobs::now_epoch() - 10 * 3600));
    const auto second = maintenance.run(Jobs::get(), t0 + std::chrono::seconds(60));
    EXPECT_EQ(second.trimmed, 0) << "not again within the interval";
    const auto third = maintenance.run(Jobs::get(), t0 + std::chrono::seconds(601));
    EXPECT_GE(third.trimmed, 1);
}

}  // namespace
