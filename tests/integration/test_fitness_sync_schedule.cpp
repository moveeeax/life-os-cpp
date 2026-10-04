/**
 * @file test_sync_schedule.cpp
 * @brief Scheduled sync enqueue: the last-day window in the region timezone.
 *
 * The timer itself is Drogon's runEvery and is not tested here; what is tested
 * is the function it calls: correct window dates, a queued run log row and a
 * job in the queue.
 */

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "fitness/sync/SyncService.hpp"
#include "jobs/FitnessSyncHandler.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/fitness/SyncRunRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;

namespace {

class SyncScheduleTest : public TestHelpers::CoreBackedTest {
protected:
    std::string config_file_name() const override { return "sync_schedule_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override {
        cfg["jobs"]["enabled"] = true;
        cfg["jobs"]["result_ttl"] = 3600;
        cfg["fitness"]["enabled"] = true;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE sync_runs");
            return true;
        });
    }
};

}  // namespace

TEST_F(SyncScheduleTest, EnqueueRecentCoversTheWindow) {
    // 2026-09-22 01:00 +08:00.
    const long run_id = Jobs::FitnessSync::enqueue_recent(2, 1790006400 + 3600);

    Repositories::SyncRunRepository runs;
    const auto row = runs.get(run_id);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["status"], "queued");
    EXPECT_EQ((*row)["requested_start"], "2026-09-21");
    EXPECT_EQ((*row)["requested_end"], "2026-09-22");

    auto job = Jobs::get().pick({"fitness_sync"}, 1);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->payload["run_id"].get<long>(), run_id);
    EXPECT_EQ(job->payload["from"], "2026-09-21");
    EXPECT_EQ(job->payload["to"], "2026-09-22");
}

TEST_F(SyncScheduleTest, WindowDatesFollowTheRegionZone) {
    // 1790006400 = 2026-09-22 00:00 +08:00, still the 21st in UTC: the day must
    // be computed in the region timezone (cn by default), not in UTC.
    const long run_id = Jobs::FitnessSync::enqueue_recent(1, 1790006400);

    Repositories::SyncRunRepository runs;
    const auto row = runs.get(run_id);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["requested_start"], "2026-09-22");
    EXPECT_EQ((*row)["requested_end"], "2026-09-22");
    // Do not leave the queue busy for neighboring tests.
    (void)Jobs::get().pick({"fitness_sync"}, 1);
}

// ── disabled module ──────────────────────────────────────────────────────────

namespace {

class FitnessDisabledJobTest : public TestHelpers::CoreBackedTest {
protected:
    std::string config_file_name() const override { return "fitness_disabled_job_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override {
        cfg["jobs"]["enabled"] = true;
        cfg["fitness"]["enabled"] = false;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE sync_runs");
            return true;
        });
    }
};

}  // namespace

TEST_F(FitnessDisabledJobTest, ProcessJobFinishesRunAsFailedWhenModuleIsOff) {
    // A worker job with the module disabled: an honest status in the log, no
    // exception and no endless retries into the DLQ.
    Repositories::SyncRunRepository runs;
    const long run_id = runs.create("2026-09-21", "2026-09-22", Sync::kAllDataTypes);
    const auto result =
        Jobs::FitnessSync::process_job(json{{"run_id", run_id}, {"from", "2026-09-21"}, {"to", "2026-09-22"}});
    EXPECT_EQ(result["status"], "failed");
    const auto row = runs.get(run_id);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["status"], "failed");
    EXPECT_EQ((*row)["result"]["error"], "fitness_disabled");
}
