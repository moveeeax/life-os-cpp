/**
 * @file test_module_guards.cpp
 * @brief Unit tests for the lifecycle/guard contracts of modules that have
 *        no other direct coverage: Tasks, Migrations' singleton doorway, the
 *        JobQueue config accessors, the worker's log rate limiter, and the
 *        SqlErrors translate_sql wrapper. Pure — no Postgres/Redis.
 */

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <trantor/net/EventLoop.h>

#include "database/Migrations.hpp"
#include "jobs/Jobs.hpp"
#include "jobs/Worker.hpp"
#include "repositories/SqlErrors.hpp"
#include "tasks/Tasks.hpp"

namespace {

// ---- Migrations singleton doorway ------------------------------------------

TEST(MigrationsGuardTest, GetBeforeInitThrows) {
    if (Migrations::is_initialized())
        Migrations::shutdown();
    EXPECT_FALSE(Migrations::is_initialized());
    EXPECT_THROW(Migrations::get(), std::runtime_error);
}

// ---- JobQueue config accessors ---------------------------------------------

TEST(JobsAccessorTest, DefaultsVisibleThroughAccessors) {
    // A fresh (never-initialized) JobQueue exposes the documented defaults —
    // the same values Jobs::initialize() falls back to (JOBS_MAX_RETRIES=3,
    // JOBS_RESULT_TTL=86400 in docs/CONFIG.md). No Redis involved.
    Jobs::JobQueue q;
    EXPECT_FALSE(q.is_initialized());
    EXPECT_EQ(q.default_max_retries(), 3);
    EXPECT_EQ(q.result_ttl(), 86400);
}

// ---- Tasks guards ----------------------------------------------------------

TEST(TasksGuardTest, ScheduleBeforeInitThrows) {
    if (Tasks::is_initialized())
        Tasks::shutdown();
    EXPECT_THROW(Tasks::schedule_recurring("t", std::chrono::milliseconds(1000), [] {}), std::runtime_error);
}

TEST(TasksGuardTest, CancelUnknownReturnsFalse) {
    // cancel() doesn't require init and must not touch the event loop for a
    // miss — safe to call in a unit test.
    EXPECT_FALSE(Tasks::cancel("does-not-exist"));
}

// A tick whose interval is longer than the time between deploys never fired:
// first_run runs it once soon after scheduling, and only once.
TEST(TasksScheduleTest, FirstRunFiresOnceBeforeTheFirstInterval) {
    if (Tasks::is_initialized())
        Tasks::shutdown();
    // The main thread already owns Drogon's loop, and waking a loop from
    // another thread trips TSan inside trantor's eventfd handling: the loop,
    // the scheduling and the run all live on one thread of the test's own.
    int with_first = 0;
    int without = 0;
    bool scheduled = false;
    bool cancelled = false;
    std::thread runner([&] {
        trantor::EventLoop loop;
        Tasks::detail::loop_override() = &loop;
        Tasks::initialize();
        scheduled = Tasks::schedule_recurring(
                        "first-run", std::chrono::hours(1), [&] { ++with_first; }, std::chrono::milliseconds(10)) &&
                    Tasks::schedule_recurring("no-first-run", std::chrono::hours(1), [&] { ++without; });
        loop.runAfter(0.5, [&] { loop.quit(); });
        loop.loop();
        cancelled = Tasks::cancel("first-run");
        Tasks::shutdown();
        Tasks::detail::loop_override() = nullptr;
    });
    runner.join();
    EXPECT_TRUE(scheduled);
    EXPECT_EQ(with_first, 1);
    EXPECT_EQ(without, 0) << "runEvery alone waits a whole interval";
    EXPECT_TRUE(cancelled);
}

// ---- the worker's rate-limited maintenance log ------------------------------

TEST(RateLimitedLogTest, LetsOneThroughPerIntervalAndCountsTheRest) {
    Jobs::RateLimitedLog log(std::chrono::seconds(60));
    const auto t0 = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
    EXPECT_TRUE(log.allow(t0)) << "the first failure is always logged";
    EXPECT_EQ(log.suppressed(), 0);
    EXPECT_FALSE(log.allow(t0 + std::chrono::seconds(1)));
    EXPECT_FALSE(log.allow(t0 + std::chrono::seconds(59)));
    EXPECT_TRUE(log.allow(t0 + std::chrono::seconds(60)));
    EXPECT_EQ(log.suppressed(), 2) << "the report carries the failures held back";
    EXPECT_TRUE(log.allow(t0 + std::chrono::seconds(200)));
    EXPECT_EQ(log.suppressed(), 0);
}

TEST(WorkerOutcomeTest, LabelsMatchTheMetricsVocabulary) {
    EXPECT_STREQ(Jobs::outcome_label(Jobs::Outcome::success), "success");
    EXPECT_STREQ(Jobs::outcome_label(Jobs::Outcome::deferred), "deferred");
    EXPECT_STREQ(Jobs::outcome_label(Jobs::Outcome::permanent_failure), "permanent_failure");
    EXPECT_STREQ(Jobs::outcome_label(Jobs::Outcome::failure), "failure");
}

// ---- SqlErrors::translate_sql ----------------------------------------------

TEST(SqlErrorsTest, ReturnsBodyResultWhenNoError) {
    int calls = 0;
    auto translator = [&](std::string_view) { ++calls; };  // must NOT be called
    int r = Repositories::detail::translate_sql([] { return 42; }, translator);
    EXPECT_EQ(r, 42);
    EXPECT_EQ(calls, 0);
}

TEST(SqlErrorsTest, NonSqlErrorPropagatesUnchangedAndSkipsTranslator) {
    int calls = 0;
    auto translator = [&](std::string_view) { ++calls; };
    EXPECT_THROW(Repositories::detail::translate_sql(
                     [] {
                         throw std::runtime_error("not a sql_error");
                         return 0;
                     },
                     translator),
                 std::runtime_error);
    EXPECT_EQ(calls, 0) << "translator must only fire on pqxx::sql_error";
}

}  // namespace
