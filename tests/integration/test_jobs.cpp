/**
 * @file test_jobs.cpp
 * @brief Integration tests for JobQueue: submit / pick / complete / fail-retry / cancel.
 * @details Requires live Redis. Tests cover the retry path and the cancel
 *          semantics (completed/failed jobs cannot be cancelled), including
 *          concurrent cancels of one job: exactly one of them wins.
 */

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
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
#include "test_helpers.hpp"

using json = nlohmann::json;

namespace {

class JobsIntegrationTest : public TestHelpers::CoreBackedTest {
protected:
    // Queue names used by the tests below; TearDown drains exactly these.
    static constexpr const char* kQueues[] = {"default",
                                              "retryq",
                                              "cancelq",
                                              "pagedq",
                                              "healq",
                                              "depthq",
                                              "backoffq",
                                              "leaseq",
                                              "deferq",
                                              "hookq",
                                              "hookq-throws",
                                              "multiq-a",
                                              "multiq-b"};

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
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    EXPECT_EQ(Jobs::get().promote_due_jobs(), 0);
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

TEST_F(JobsIntegrationTest, BackoffDelaysRequeueUntilDue) {
    Jobs::get().set_retry_backoff(/*base_ms=*/200, /*max_ms=*/1000);

    auto job = Jobs::get().submit("backoffq", {});
    track(job.id);

    auto p1 = Jobs::get().pick({"backoffq"}, 2, "w1");
    ASSERT_TRUE(p1);
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
    EXPECT_EQ(Jobs::get().promote_due_jobs(), 0);

    // After the backoff window, the job is promoted back and pickable again.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    EXPECT_EQ(Jobs::get().promote_due_jobs(), 1);

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

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    EXPECT_EQ(Jobs::get().promote_due_jobs(), 1) << "promoted with retry backoff off too";
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

}  // namespace
