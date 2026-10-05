/**
 * @file test_sync_schedule.cpp
 * @brief Scheduled sync enqueue: the last-day window in the region timezone.
 *
 * The timer itself is Drogon's runEvery and is not tested here; what is tested
 * is the function it calls: correct window dates, a queued run log row and a
 * job in the queue.
 */

#include <algorithm>
#include <string>
#include <vector>

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

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa2";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb2";
constexpr const char* kVera = "cccccccc-cccc-4ccc-8ccc-ccccccccccc2";

class SyncScheduleTest : public TestHelpers::CoreBackedTest {
protected:
    std::string config_file_name() const override { return "sync_schedule_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override {
        cfg["jobs"]["enabled"] = true;
        cfg["jobs"]["result_ttl"] = 3600;
        cfg["fitness"]["enabled"] = true;
        cfg["fitness"]["xiaomi"]["token_key"] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
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
    const long run_id = Jobs::FitnessSync::enqueue_recent({kAnna, "1111111111", "cn"}, 2, 1790006400 + 3600);

    Repositories::SyncRunRepository runs;
    const auto row = runs.get(run_id, "1111111111");
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["status"], "queued");
    EXPECT_EQ((*row)["requested_start"], "2026-09-21");
    EXPECT_EQ((*row)["requested_end"], "2026-09-22");

    auto job = Jobs::get().pick({"fitness_sync"}, 1);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->payload["run_id"].get<long>(), run_id);
    // The job says whose account it syncs.
    EXPECT_EQ(job->payload["owner_id"], kAnna);
    EXPECT_EQ(job->payload["from"], "2026-09-21");
    EXPECT_EQ(job->payload["to"], "2026-09-22");
}

TEST_F(SyncScheduleTest, WindowDatesFollowTheAccountsRegion) {
    // 1790006400 = 2026-09-22 00:00 +08:00, still the 21st in UTC: the day is
    // computed in the zone of the account's region.
    Repositories::SyncRunRepository runs;
    const long cn = Jobs::FitnessSync::enqueue_recent({kAnna, "1111111111", "cn"}, 1, 1790006400);
    EXPECT_EQ((*runs.get(cn, "1111111111"))["requested_start"], "2026-09-22");
    EXPECT_EQ((*runs.get(cn, "1111111111"))["requested_end"], "2026-09-22");

    const long sg = Jobs::FitnessSync::enqueue_recent({kBoris, "2222222222", "sg"}, 1, 1790006400);
    EXPECT_EQ((*runs.get(sg, "2222222222"))["requested_start"], "2026-09-21");
    // Do not leave the queue busy for neighboring tests.
    (void)Jobs::get().pick({"fitness_sync"}, 1);
    (void)Jobs::get().pick({"fitness_sync"}, 1);
}

TEST_F(SyncScheduleTest, TickEnqueuesOneJobPerAccountWithAnAcceptedToken) {
    Database::get().execute_write([](auto& txn) {
        txn.exec("TRUNCATE TABLE mi_accounts");
        for (const char* id : {kAnna, kBoris, kVera}) {
            txn.exec_params(
                "INSERT INTO users (id, email, confirmed, role_id) "
                "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                "ON CONFLICT DO NOTHING",
                std::string(id),
                std::string(id) + "@example.test");
        }
        txn.exec_params(
            "INSERT INTO mi_accounts (owner_id, xiaomi_user_id, pass_token_sealed, nonce, region, status) VALUES "
            "($1::uuid, '1111111111', 'x', 'x', 'cn', 'ok'), "
            "($2::uuid, '2222222222', 'x', 'x', 'sg', 'reauth_required'), "
            "($3::uuid, '3333333333', 'x', 'x', 'de', 'ok')",
            std::string(kAnna),
            std::string(kBoris),
            std::string(kVera));
        return true;
    });

    EXPECT_EQ(Jobs::FitnessSync::enqueue_recent_for_all(2, 1790006400 + 3600), 2);

    // Boris has to link again: a sync of his account could only fail.
    std::vector<std::string> owners;
    for (int i = 0; i < 3; ++i) {
        auto job = Jobs::get().pick({"fitness_sync"}, 1);
        if (job.has_value()) {
            owners.push_back(job->payload.value("owner_id", std::string()));
        }
    }
    std::sort(owners.begin(), owners.end());
    EXPECT_EQ(owners, (std::vector<std::string>{kAnna, kVera}));
    const long runs_of_boris = Database::get().execute_read([](auto& txn) {
        return txn.exec("SELECT COUNT(*) FROM sync_runs WHERE xiaomi_user_id = '2222222222'")[0][0].template as<long>();
    });
    EXPECT_EQ(runs_of_boris, 0);

    Database::get().execute_write([](auto& txn) {
        txn.exec("TRUNCATE TABLE mi_accounts");
        return true;
    });
    EXPECT_EQ(Jobs::FitnessSync::enqueue_recent_for_all(2, 1790006400), 0);
}

// A job enqueued before runs had an owner cannot be attributed to an account.
TEST_F(SyncScheduleTest, JobWithoutAnOwnerFailsHonestly) {
    Repositories::SyncRunRepository runs;
    const long run_id = runs.create("1111111111", "2026-09-21", "2026-09-22", Sync::kAllDataTypes);
    const auto result =
        Jobs::FitnessSync::process_job(json{{"run_id", run_id}, {"from", "2026-09-21"}, {"to", "2026-09-22"}});
    EXPECT_EQ(result["status"], "failed");
    EXPECT_EQ((*runs.get(run_id, "1111111111"))["result"]["error"], "outdated_job");
}

// The link was removed between the enqueue and the run.
TEST_F(SyncScheduleTest, JobOfAnUnlinkedUserFailsAsNotLinked) {
    Database::get().execute_write([](auto& txn) {
        txn.exec("TRUNCATE TABLE mi_accounts");
        return true;
    });
    Repositories::SyncRunRepository runs;
    const long run_id = runs.create("1111111111", "2026-09-21", "2026-09-22", Sync::kAllDataTypes);
    const auto result = Jobs::FitnessSync::process_job(
        json{{"run_id", run_id}, {"owner_id", kAnna}, {"from", "2026-09-21"}, {"to", "2026-09-22"}});
    EXPECT_EQ(result["status"], "failed");
    EXPECT_EQ((*runs.get(run_id, "1111111111"))["result"]["error"], "not_linked");
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
    const long run_id = runs.create("1111111111", "2026-09-21", "2026-09-22", Sync::kAllDataTypes);
    const auto result =
        Jobs::FitnessSync::process_job(json{{"run_id", run_id}, {"from", "2026-09-21"}, {"to", "2026-09-22"}});
    EXPECT_EQ(result["status"], "failed");
    const auto row = runs.get(run_id, "1111111111");
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["status"], "failed");
    EXPECT_EQ((*row)["result"]["error"], "fitness_disabled");
}
