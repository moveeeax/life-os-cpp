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
#include "fitness/jobs/SyncJob.hpp"
#include "fitness/sync/SyncService.hpp"
#include "jobs/Job.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/fitness/MiAccountRepository.hpp"
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
    // Exactly as many picks as jobs: a pick on an empty queue blocks past the
    // Redis socket timeout of the test config.
    for (int i = 0; i < 2; ++i) {
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

// The tick after every API start skips an account synced within half the interval.
TEST_F(SyncScheduleTest, TickLeavesARecentlySyncedAccountAlone) {
    Database::get().execute_write([](auto& txn) {
        txn.exec("TRUNCATE TABLE mi_accounts");
        txn.exec_params(
            "INSERT INTO users (id, email, confirmed, role_id) "
            "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) ON CONFLICT DO NOTHING",
            std::string(kAnna),
            std::string(kAnna) + "@example.test");
        txn.exec_params(
            "INSERT INTO mi_accounts (owner_id, xiaomi_user_id, pass_token_sealed, nonce, region, status) VALUES "
            "($1::uuid, '1111111111', 'x', 'x', 'cn', 'ok')",
            std::string(kAnna));
        return true;
    });
    Repositories::SyncRunRepository runs;
    const long earlier = runs.create("1111111111", "2026-09-21", "2026-09-22", Sync::kAllDataTypes);
    runs.finish(earlier, "succeeded", json::object());

    EXPECT_EQ(Jobs::FitnessSync::enqueue_recent_for_all(2, 1790006400, 7200), 0) << "synced a moment ago";
    runs.finish(earlier, "failed", json::object());
    EXPECT_EQ(Jobs::FitnessSync::enqueue_recent_for_all(2, 1790006400, 7200), 1) << "a failed run does not count";
    (void)Jobs::get().pick({"fitness_sync"}, 1);

    Database::get().execute_write([](auto& txn) {
        txn.exec("TRUNCATE TABLE mi_accounts");
        return true;
    });
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

namespace {

constexpr const char* kKeyB64 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";

/// Anna as a user row; with @p readable her link carries a token sealed with
/// the test key, otherwise a stored value that cannot be unsealed.
void link_anna(bool readable) {
    Database::get().execute_write([](auto& txn) {
        txn.exec("TRUNCATE TABLE mi_accounts");
        txn.exec_params(
            "INSERT INTO users (id, email, confirmed, role_id) "
            "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
            "ON CONFLICT DO NOTHING",
            std::string(kAnna),
            std::string(kAnna) + "@example.test");
        return true;
    });
    if (readable) {
        Repositories::MiAccountRepository(kKeyB64).link(kAnna, {"1111111111", std::string(347, 'S'), "cn"}, true);
        return;
    }
    Database::get().execute_write([](auto& txn) {
        txn.exec_params(
            "INSERT INTO mi_accounts (owner_id, xiaomi_user_id, pass_token_sealed, nonce, region, status) "
            "VALUES ($1::uuid, '1111111111', 'x', 'x', 'cn', 'ok')",
            std::string(kAnna));
        return true;
    });
}

void set_run(long run_id, const std::string& status, const std::string& heartbeat_age) {
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params(
            "UPDATE sync_runs SET status = $2, started_at = now() - interval '1 hour', "
            "heartbeat_at = now() - $3::interval WHERE id = $1",
            run_id,
            status,
            heartbeat_age);
        return true;
    });
}

}  // namespace

// review-fitness 3: a token the worker cannot unseal (key rotated, corrupt row)
// ends the run as failed and asks for a new link, instead of retrying into the
// DLQ with the run left queued and the link still ok.
TEST_F(SyncScheduleTest, AnUnreadableTokenFailsTheRunAndAsksToLinkAgain) {
    link_anna(/*readable=*/false);
    Repositories::SyncRunRepository runs;
    const long run_id = runs.create("1111111111", "2026-09-21", "2026-09-22", Sync::kAllDataTypes);

    json result;
    ASSERT_NO_THROW(result = Jobs::FitnessSync::process_job(
                        json{{"run_id", run_id}, {"owner_id", kAnna}, {"from", "2026-09-21"}, {"to", "2026-09-22"}}));
    EXPECT_EQ(result["status"], "failed");
    const auto row = runs.get(run_id, "1111111111");
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["status"], "failed");
    EXPECT_EQ((*row)["result"]["error"], "token_unreadable");
    const auto link = Repositories::MiAccountRepository("").status(kAnna);
    ASSERT_TRUE(link.has_value());
    EXPECT_EQ((*link)["status"], "reauth_required");
    // The scheduled tick leaves the account alone until it is linked again.
    EXPECT_EQ(Jobs::FitnessSync::enqueue_recent_for_all(2, 1790006400), 0);
}

// review-jobs 4: a delivery of a job whose run still beats under another
// delivery waits (DeferJob, no attempt spent) instead of finishing as skipped.
TEST_F(SyncScheduleTest, ARunThatStillBeatsElsewhereDefersTheJob) {
    link_anna(/*readable=*/true);
    Repositories::SyncRunRepository runs;
    const long run_id = runs.create("1111111111", "2026-09-21", "2026-09-22", Sync::kAllDataTypes);
    set_run(run_id, "running", "0 seconds");

    try {
        Jobs::FitnessSync::process_job(
            json{{"run_id", run_id}, {"owner_id", kAnna}, {"from", "2026-09-21"}, {"to", "2026-09-22"}});
        ADD_FAILURE() << "expected DeferJob";
    } catch (const Jobs::DeferJob& e) {
        EXPECT_GT(e.delay_ms, 0);
        EXPECT_LE(e.delay_ms, (Sync::SyncService::quiet_after_seconds() + 1) * 1000);
    }
    EXPECT_EQ((*runs.get(run_id, "1111111111"))["status"], "running");
}

// review-jobs 4: the dead-letter hook closes a run its job will never finish:
// a queued one, or a running one whose heartbeat is quiet. A run that still
// beats belongs to a live delivery and a closed run stays as it is.
TEST_F(SyncScheduleTest, ADeadLetteredJobClosesItsAbandonedRun) {
    Repositories::SyncRunRepository runs;
    const auto payload = [](long id) {
        return json{{"run_id", id}, {"owner_id", kAnna}, {"from", "2026-09-21"}, {"to", "2026-09-22"}};
    };
    const long queued = runs.create("1111111111", "2026-09-21", "2026-09-22", {"sleep"});
    const long quiet = runs.create("2222222222", "2026-09-21", "2026-09-22", {"sleep"});
    set_run(quiet, "running", "1 hour");
    const long live = runs.create("3333333333", "2026-09-21", "2026-09-22", {"sleep"});
    set_run(live, "running", "0 seconds");
    const long done = runs.create("4444444444", "2026-09-21", "2026-09-22", {"sleep"});
    runs.finish(done, "succeeded", json{{"sleep", {{"added", 1}}}});

    for (const long id : {queued, quiet, live, done}) {
        Jobs::FitnessSync::on_dead_letter(payload(id), "boom");
    }

    const auto status_of = [&](long id, const char* account) { return (*runs.get(id, account))["status"]; };
    EXPECT_EQ(status_of(queued, "1111111111"), "failed");
    EXPECT_EQ((*runs.get(queued, "1111111111"))["result"]["error"], "dead_lettered");
    EXPECT_EQ(status_of(quiet, "2222222222"), "failed");
    EXPECT_EQ(status_of(live, "3333333333"), "running");
    EXPECT_EQ(status_of(done, "4444444444"), "succeeded");
    // A payload without a run id is ignored.
    EXPECT_NO_THROW(Jobs::FitnessSync::on_dead_letter(json{{"owner_id", kAnna}}, "boom"));
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
