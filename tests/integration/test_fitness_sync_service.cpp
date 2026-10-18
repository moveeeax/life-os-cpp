/**
 * @file test_sync_service.cpp
 * @brief The whole sync: the cloud is a fake, the database is real.
 *
 * Plan Review Focus: idempotency of a repeated pass, isolation of a failed
 * type, the auth error class, mutual exclusion of runs, sync_state updates.
 */

#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "fitness/FakeHttpTransport.hpp"
#include "fitness/sync/SyncService.hpp"
#include "fitness/xiaomi/Crypto.hpp"
#include "fitness/xiaomi/Regions.hpp"
#include "repositories/fitness/SyncRunRepository.hpp"
#include "test_helpers.hpp"

namespace {

using nlohmann::json;

constexpr long long kNoon = 1790049600;  // 2026-09-22T12:00:00+08:00

// The account every run here belongs to: the id the scripted login answers with.
constexpr const char* kAccount = "1234567890";

class SyncServiceTest : public TestHelpers::CoreBackedTest {
protected:
    FakeHttpTransport transport;

    std::string config_file_name() const override { return "sync_service_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec(
                "TRUNCATE TABLE daily_activity, sleep_sessions, workouts, body_measurements, "
                "heart_rate_samples, spo2_samples, stress_samples, abnormal_heart_beat_events, "
                "sync_state, sync_runs");
            return true;
        });
    }

    static std::string steps_page(int steps) {
        const json record{{"time", kNoon},
                          {"zone_offset", 28800},
                          {"zone_name", "Asia/Shanghai"},
                          {"value", json{{"steps", steps}, {"distance", 10.0}, {"calories", 1.0}}.dump()}};
        return json{{"code", 0}, {"result", {{"data_list", json::array({record})}, {"has_more", false}}}}.dump();
    }

    static std::string steps_page_at(long long time, int zone_offset, int steps) {
        const json record{{"time", time},
                          {"zone_offset", zone_offset},
                          {"value", json{{"steps", steps}, {"distance", 10.0}, {"calories", 1.0}}.dump()}};
        return json{{"code", 0}, {"result", {{"data_list", json::array({record})}, {"has_more", false}}}}.dump();
    }

    static std::string weight_page() {
        const json record{{"time", kNoon}, {"zone_offset", 28800}, {"value", json{{"weight", 91.9}}.dump()}};
        return json{{"code", 0}, {"result", {{"data_list", json::array({record})}, {"has_more", false}}}}.dump();
    }

    static std::string sleep_page(long long bed, long long wake) {
        const json value{{"bedtime", bed}, {"wake_up_time", wake}};
        const json record{{"time", wake},
                          {"zone_offset", 28800},
                          {"zone_name", "Asia/Shanghai"},
                          {"sid", "band-1"},
                          {"value", value.dump()}};
        return json{{"code", 0}, {"result", {{"data_list", json::array({record})}, {"has_more", false}}}}.dump();
    }

    static std::string empty_page() {
        return json{{"code", 0}, {"result", {{"data_list", json::array()}, {"has_more", false}}}}.dump();
    }

    Sync::SyncService service() { return Sync::SyncService(transport, {"1234567890", std::string(347, 'S'), "cn"}); }
};

}  // namespace

// A day cut by a chunk boundary is assembled whole. Chunk boundaries follow the
// region timezone (+08 for cn), the device lives in +07: the evening minutes of
// day X fall into the next chunk, and per-chunk row upserts overwrote the full
// day aggregate with a one-hour fragment. The live backfill lost every seventh
// day this way (comparison with the Python bridge, 2026-09 report).
TEST_F(SyncServiceTest, ChunkBoundaryDayIsAggregatedAcrossChunks) {
    Repositories::SyncRunRepository runs;
    transport.reply_login();
    // Chunk 1 (2026-09-22..28): the full day of the 28th, noon +08.
    transport.reply_encrypted(steps_page_at(1790568000, 28800, 7000));  // steps
    transport.reply_encrypted(empty_page());                            // calories
    // Chunk 2 (2026-09-29..30): an evening minute of the same 28th in +07.
    transport.reply_encrypted(steps_page_at(1790613000, 25200, 226));  // steps
    transport.reply_encrypted(empty_page());                           // calories

    const long id = runs.create(kAccount, "2026-09-22", "2026-09-30", {"daily_activity"});
    service().run(id, "2026-09-22", "2026-09-30", {"daily_activity"});

    const auto row = runs.get(id, kAccount);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["status"], "succeeded");
    const long steps = Database::get().execute_read([](auto& txn) {
        auto r = txn.exec("SELECT steps FROM daily_activity WHERE date = '2026-09-28'");
        return r.empty() ? -1L : r[0][0].template as<long>();
    });
    EXPECT_EQ(steps, 7226);
}

// Findings of the final plan 2 review. The five behaviors below close
// Critical 1-2 and Important 3-5 of the reviewer's report.

// Critical 1: the day at the edge of the requested range is partial by
// definition and must not be upserted: the window is cut in the region
// timezone, and an evening minute of the previous day falls inside. A full day
// in the database must not be overwritten.
TEST_F(SyncServiceTest, RangeEdgeDayOutsideRequestIsNotUpserted) {
    Repositories::SyncRunRepository runs;
    transport.reply_login();
    transport.reply_encrypted(steps_page_at(1790568000, 28800, 7000));  // chunk 22..28
    transport.reply_encrypted(empty_page());
    transport.reply_encrypted(steps_page_at(1790613000, 25200, 226));  // chunk 29..30
    transport.reply_encrypted(empty_page());
    const long full = runs.create(kAccount, "2026-09-22", "2026-09-30", {"daily_activity"});
    service().run(full, "2026-09-22", "2026-09-30", {"daily_activity"});

    // Incremental run from the next day: the same evening minute of the 28th
    // falls into the 29..30 window, but the 28th is outside the request.
    transport.reply_login();
    transport.reply_encrypted(steps_page_at(1790613000, 25200, 226));
    transport.reply_encrypted(empty_page());
    const long tail = runs.create(kAccount, "2026-09-29", "2026-09-30", {"daily_activity"});
    service().run(tail, "2026-09-29", "2026-09-30", {"daily_activity"});

    const long steps = Database::get().execute_read([](auto& txn) {
        auto r = txn.exec("SELECT steps FROM daily_activity WHERE date = '2026-09-28'");
        return r.empty() ? -1L : r[0][0].template as<long>();
    });
    EXPECT_EQ(steps, 7226);
}

// Important 4: a closed run is not revived by a redelivered job.
TEST_F(SyncServiceTest, FinishedRunCannotBeRestarted) {
    Repositories::SyncRunRepository runs;
    transport.reply_login();
    transport.reply_encrypted(steps_page(100));
    transport.reply_encrypted(empty_page());
    const long id = runs.create(kAccount, "2026-09-22", "2026-09-22", {"daily_activity"});
    service().run(id, "2026-09-22", "2026-09-22", {"daily_activity"});

    // The fake's queue is empty: the repeated attempt must finish before any network call.
    const auto second = service().run(id, "2026-09-22", "2026-09-22", {"daily_activity"});

    EXPECT_TRUE(second.contains("skipped_reason"));
    const auto row = runs.get(id, kAccount);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["status"], "succeeded");
}

// Critical 2: a stuck running row (worker killed mid-sync) must not block all
// future runs. The stale row is marked interrupted.
TEST_F(SyncServiceTest, StaleRunningRunIsInterruptedAndReleasesTheMutex) {
    Repositories::SyncRunRepository runs;
    Database::get().execute_write([](auto& txn) {
        txn.exec(
            "INSERT INTO sync_runs (status, started_at, requested_start, requested_end, data_types, xiaomi_user_id) "
            "VALUES ('running', now() - interval '6 hours', '2026-09-01', '2026-09-02', '{daily_activity}', "
            "'1234567890')");
        return true;
    });

    transport.reply_login();
    transport.reply_encrypted(steps_page(100));
    transport.reply_encrypted(empty_page());
    const long id = runs.create(kAccount, "2026-09-22", "2026-09-22", {"daily_activity"});
    const auto result = service().run(id, "2026-09-22", "2026-09-22", {"daily_activity"});

    EXPECT_FALSE(result.contains("skipped_reason"));
    const auto row = runs.get(id, kAccount);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["status"], "succeeded");
    const std::string stale = Database::get().execute_read([](auto& txn) {
        auto r = txn.exec("SELECT status FROM sync_runs WHERE requested_start = '2026-09-01'");
        return r[0][0].template as<std::string>();
    });
    EXPECT_EQ(stale, "interrupted");
}

// Important 3: the daily sleep reports window takes a day of margin on both
// sides, like the reference: the report timezone may differ from the region's.
TEST_F(SyncServiceTest, SleepReportWindowHasOneDayMargin) {
    Repositories::SyncRunRepository runs;
    transport.reply_login();
    // A session without a score, wake-up 2026-09-24 07:00 +08.
    transport.reply_encrypted(sleep_page(1790172000, 1790204400));
    transport.reply_encrypted(empty_page());  // reports: empty

    const long id = runs.create(kAccount, "2026-09-22", "2026-09-28", {"sleep"});
    service().run(id, "2026-09-22", "2026-09-28", {"sleep"});

    // The reports request is the fourth one (two logins, sleep fetch, reports).
    ASSERT_EQ(transport.requests().size(), 4u);
    const auto& reports_req = transport.requests().back();
    const std::string nonce = Xiaomi::Crypto::b64_decode(FakeHttpTransport::form_value(reports_req.body, "_nonce"));
    const std::string signed_nonce = Xiaomi::Crypto::signed_nonce(FakeHttpTransport::kSsecurityB64, nonce);
    const std::string decrypted = Xiaomi::Crypto::rc4(
        signed_nonce, Xiaomi::Crypto::b64_decode(FakeHttpTransport::form_value(reports_req.body, "data")));
    const auto payload = json::parse(decrypted);
    const auto expected = Xiaomi::range_to_timestamps("2026-09-23", "2026-09-25", "cn");
    EXPECT_EQ(payload["start_time"].get<long long>(), expected.first);
    EXPECT_EQ(payload["end_time"].get<long long>(), expected.second);
}

// Important 5: an auth failure at login closes the remaining types without
// contacting the cloud. Repeated logins with a dead token burn attempts.
TEST_F(SyncServiceTest, AuthFailureOnLoginStopsFurtherLoginAttempts) {
    Repositories::SyncRunRepository runs;
    transport.reply({401, "", {}});

    const long id = runs.create(kAccount, "2026-09-22", "2026-09-22", {"daily_activity", "sleep"});
    service().run(id, "2026-09-22", "2026-09-22", {"daily_activity", "sleep"});

    const auto row = runs.get(id, kAccount);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["status"], "failed");
    EXPECT_EQ((*row)["result"]["daily_activity"]["error"], "auth");
    EXPECT_EQ((*row)["result"]["sleep"]["error"], "auth");
    // One request for the whole run: the second type never hit the network.
    EXPECT_EQ(transport.requests().size(), 1u);
}

TEST_F(SyncServiceTest, RepeatRunGivesAddedThenUpdated) {
    Repositories::SyncRunRepository runs;

    transport.reply_login();
    transport.reply_encrypted(steps_page(100));  // steps
    transport.reply_encrypted(empty_page());     // calories
    const long first = runs.create(kAccount, "2026-09-22", "2026-09-22", {"daily_activity"});
    service().run(first, "2026-09-22", "2026-09-22", {"daily_activity"});
    auto row = runs.get(first, kAccount);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["status"], "succeeded");
    EXPECT_EQ((*row)["result"]["daily_activity"]["added"], 1);

    transport.reply_login();
    transport.reply_encrypted(steps_page(100));
    transport.reply_encrypted(empty_page());
    const long second = runs.create(kAccount, "2026-09-22", "2026-09-22", {"daily_activity"});
    service().run(second, "2026-09-22", "2026-09-22", {"daily_activity"});
    row = runs.get(second, kAccount);
    EXPECT_EQ((*row)["result"]["daily_activity"]["added"], 0);
    EXPECT_EQ((*row)["result"]["daily_activity"]["updated"], 1);
}

// A failed type does not cancel the others, and its error class is visible in the log.
TEST_F(SyncServiceTest, FailedTypeDoesNotStopTheRest) {
    Repositories::SyncRunRepository runs;
    transport.reply_login();
    // 400 is not retried: the client now retries 5xx and would eat the neighbor's responses.
    transport.reply({400, "boom", {}});        // sleep: protocol error
    transport.reply_encrypted(weight_page());  // body_measurements works

    const long id = runs.create(kAccount, "2026-09-22", "2026-09-22", {"sleep", "body_measurements"});
    service().run(id, "2026-09-22", "2026-09-22", {"sleep", "body_measurements"});

    const auto row = runs.get(id, kAccount);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["status"], "failed");
    EXPECT_EQ((*row)["result"]["sleep"]["error"], "protocol");
    EXPECT_EQ((*row)["result"]["body_measurements"]["added"], 1);
}

// An auth failure is marked separately: it is fixed by a fresh token, and the
// queue must not retry it.
TEST_F(SyncServiceTest, AuthCodeIsClassifiedAsAuth) {
    Repositories::SyncRunRepository runs;
    transport.reply_login();
    transport.reply_encrypted(R"({"code":-10001,"message":"expired"})");
    transport.reply_encrypted(weight_page());

    const long id = runs.create(kAccount, "2026-09-22", "2026-09-22", {"daily_activity", "body_measurements"});
    service().run(id, "2026-09-22", "2026-09-22", {"daily_activity", "body_measurements"});

    const auto row = runs.get(id, kAccount);
    EXPECT_EQ((*row)["result"]["daily_activity"]["error"], "auth");
    EXPECT_EQ((*row)["result"]["body_measurements"]["added"], 1);
}

// A second running row is impossible: the queued -> running transition is
// atomic via a partial unique index, and the competitor ends as skipped.
TEST_F(SyncServiceTest, ConcurrentRunIsSkipped) {
    Repositories::SyncRunRepository runs;
    Database::get().execute_write([](auto& txn) {
        txn.exec(
            "INSERT INTO sync_runs (status, requested_start, requested_end, xiaomi_user_id) "
            "VALUES ('running', '2026-09-01', '2026-09-07', '1234567890')");
        return true;
    });

    const long id = runs.create(kAccount, "2026-09-22", "2026-09-22", {"daily_activity"});
    service().run(id, "2026-09-22", "2026-09-22", {"daily_activity"});

    const auto row = runs.get(id, kAccount);
    EXPECT_EQ((*row)["status"], "skipped");
    EXPECT_TRUE(transport.requests().empty()) << "must not reach the cloud";
}

TEST_F(SyncServiceTest, SyncStateIsUpdatedPerType) {
    Repositories::SyncRunRepository runs;
    transport.reply_login();
    transport.reply_encrypted(steps_page(100));
    transport.reply_encrypted(empty_page());

    const long id = runs.create(kAccount, "2026-09-22", "2026-09-22", {"daily_activity"});
    service().run(id, "2026-09-22", "2026-09-22", {"daily_activity"});

    const bool has_state = Database::get().execute_read([](auto& txn) {
        auto r = txn.exec(
            "SELECT last_sync_at IS NOT NULL AND records_count = 1 FROM sync_state "
            "WHERE data_type = 'daily_activity' AND xiaomi_user_id = '1234567890'");
        return !r.empty() && r[0][0].template as<bool>();
    });
    EXPECT_TRUE(has_state);
}

// Accounts sync independently: a live run of another account neither skips
// this one nor is touched by it, however old it is.
TEST_F(SyncServiceTest, RunOfAnotherAccountDoesNotBlockOrGetInterrupted) {
    Repositories::SyncRunRepository runs;
    Database::get().execute_write([](auto& txn) {
        txn.exec(
            "INSERT INTO sync_runs (status, started_at, requested_start, requested_end, xiaomi_user_id) "
            "VALUES ('running', now() - interval '6 hours', '2026-09-01', '2026-09-07', '5555555555')");
        return true;
    });

    transport.reply_login();
    transport.reply_encrypted(steps_page(100));
    transport.reply_encrypted(empty_page());
    const long id = runs.create(kAccount, "2026-09-22", "2026-09-22", {"daily_activity"});
    const auto result = service().run(id, "2026-09-22", "2026-09-22", {"daily_activity"});

    EXPECT_FALSE(result.contains("skipped_reason"));
    EXPECT_EQ((*runs.get(id, kAccount))["status"], "succeeded");
    const std::string other = Database::get().execute_read([](auto& txn) {
        return txn.exec("SELECT status FROM sync_runs WHERE xiaomi_user_id = '5555555555'")[0][0]
            .template as<std::string>();
    });
    EXPECT_EQ(other, "running") << "a stale run of another account is that account's to take over";
    EXPECT_TRUE(runs.any_running("5555555555"));
    EXPECT_FALSE(runs.any_running(kAccount));
    // The run is invisible under another account's id.
    EXPECT_FALSE(runs.get(id, "5555555555").has_value());
}

// review-fitness 7: a login that fails on the network is not repeated for each
// type: one request for the whole run, every type closed with the same class.
TEST_F(SyncServiceTest, ALoginNetworkFailureIsTriedOncePerRun) {
    Repositories::SyncRunRepository runs;
    transport.reply_transport_error("connection refused");

    const std::vector<std::string> types{"daily_activity", "sleep", "heart_rate"};
    const long id = runs.create(kAccount, "2026-09-22", "2026-09-22", types);
    auto svc = service();
    svc.run(id, "2026-09-22", "2026-09-22", types);

    const auto row = runs.get(id, kAccount);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["status"], "failed");
    for (const auto& type : types) {
        EXPECT_EQ((*row)["result"][type]["error"], "protocol") << type;
    }
    EXPECT_EQ(transport.requests().size(), 1u);
    // Xiaomi never answered: the link status is left as it was.
    EXPECT_EQ(svc.login_outcome(), Sync::SyncService::Login::NotAttempted);
}

// review-fitness 2: steps are bucketed by the device's zone, the window by the
// region's. The activity fetch takes a day of margin on both sides, so the
// local early hours of `from` (a device ahead of the region) and the late
// hours of `to` (a device behind) are inside it.
TEST_F(SyncServiceTest, ActivityFetchHasADayOfMarginOnBothSides) {
    Repositories::SyncRunRepository runs;
    transport.reply_login();
    transport.reply_encrypted(steps_page(100));
    transport.reply_encrypted(empty_page());

    const long id = runs.create(kAccount, "2026-09-22", "2026-09-22", {"daily_activity"});
    service().run(id, "2026-09-22", "2026-09-22", {"daily_activity"});

    // Two login requests, then steps and calories.
    ASSERT_EQ(transport.requests().size(), 4u);
    const auto expected = Xiaomi::range_to_timestamps("2026-09-21", "2026-09-23", "cn");
    for (const std::size_t i : {std::size_t{2}, std::size_t{3}}) {
        const auto& req = transport.requests()[i];
        const std::string nonce = Xiaomi::Crypto::b64_decode(FakeHttpTransport::form_value(req.body, "_nonce"));
        const std::string signed_nonce = Xiaomi::Crypto::signed_nonce(FakeHttpTransport::kSsecurityB64, nonce);
        const auto payload = json::parse(Xiaomi::Crypto::rc4(
            signed_nonce, Xiaomi::Crypto::b64_decode(FakeHttpTransport::form_value(req.body, "data"))));
        EXPECT_EQ(payload["start_time"].get<long long>(), expected.first) << i;
        EXPECT_EQ(payload["end_time"].get<long long>(), expected.second) << i;
    }
    // Only the requested day is stored.
    const long rows = Database::get().execute_read(
        [](auto& txn) { return txn.exec("SELECT COUNT(*) FROM daily_activity")[0][0].template as<long>(); });
    EXPECT_EQ(rows, 1);
}

// review-jobs 4: a worker killed mid-run leaves its row running. The job comes
// back and takes its own run over once the heartbeat is quiet, instead of
// finishing as skipped and blocking the account for hours.
TEST_F(SyncServiceTest, ItsOwnQuietRunningRowIsTakenOver) {
    Repositories::SyncRunRepository runs;
    const long id = runs.create(kAccount, "2026-09-22", "2026-09-22", {"daily_activity"});
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params(
            "UPDATE sync_runs SET status = 'running', started_at = now() - interval '1 hour', "
            "heartbeat_at = now() - interval '1 hour' WHERE id = $1",
            id);
        return true;
    });
    transport.reply_login();
    transport.reply_encrypted(steps_page(100));
    transport.reply_encrypted(empty_page());

    auto svc = service();
    const auto result = svc.run(id, "2026-09-22", "2026-09-22", {"daily_activity"});

    EXPECT_EQ(svc.start_outcome(), Sync::SyncService::Start::Started);
    EXPECT_FALSE(result.contains("skipped_reason"));
    EXPECT_EQ((*runs.get(id, kAccount))["status"], "succeeded");
}

// ...and while the row still beats, another delivery holds it: nothing is
// touched and the caller learns how long to wait.
TEST_F(SyncServiceTest, ItsOwnRowThatStillBeatsIsHeldElsewhere) {
    Repositories::SyncRunRepository runs;
    const long id = runs.create(kAccount, "2026-09-22", "2026-09-22", {"daily_activity"});
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("UPDATE sync_runs SET status = 'running', heartbeat_at = now() WHERE id = $1", id);
        return true;
    });

    auto svc = service();
    const auto result = svc.run(id, "2026-09-22", "2026-09-22", {"daily_activity"});

    EXPECT_EQ(svc.start_outcome(), Sync::SyncService::Start::HeldElsewhere);
    EXPECT_GT(svc.retry_in_ms(), 0);
    EXPECT_TRUE(transport.requests().empty()) << "must not reach the cloud";
    const auto row = runs.get(id, kAccount);
    EXPECT_EQ((*row)["status"], "running");
    EXPECT_TRUE((*row)["result"].empty());
    EXPECT_TRUE(result.empty());
}

// A run of the same account whose heartbeat went quiet no longer holds the
// slot for the hours of the old stale rule.
TEST_F(SyncServiceTest, AQuietRunOfTheAccountIsInterruptedLongBeforeTheOldStaleLimit) {
    Repositories::SyncRunRepository runs;
    Database::get().execute_write([](auto& txn) {
        txn.exec(
            "INSERT INTO sync_runs (status, started_at, heartbeat_at, requested_start, requested_end, xiaomi_user_id) "
            "VALUES ('running', now() - interval '20 minutes', now() - interval '20 minutes', "
            "'2026-09-01', '2026-09-02', '1234567890')");
        return true;
    });
    transport.reply_login();
    transport.reply_encrypted(steps_page(100));
    transport.reply_encrypted(empty_page());

    const long id = runs.create(kAccount, "2026-09-22", "2026-09-22", {"daily_activity"});
    service().run(id, "2026-09-22", "2026-09-22", {"daily_activity"});

    EXPECT_EQ((*runs.get(id, kAccount))["status"], "succeeded");
    const std::string quiet = Database::get().execute_read([](auto& txn) {
        return txn.exec("SELECT status FROM sync_runs WHERE requested_start = '2026-09-01'")[0][0]
            .template as<std::string>();
    });
    EXPECT_EQ(quiet, "interrupted");
}

// review-jobs 5: only the unique violation means "another run is in progress".
// Any other database error while claiming propagates (the queue retries), and
// the run is not marked skipped.
TEST_F(SyncServiceTest, ADatabaseErrorWhileClaimingIsNotASkip) {
    Repositories::SyncRunRepository runs;
    const long id = runs.create(kAccount, "1999-01-01", "1999-01-01", {"daily_activity"});
    const auto sql = [](const char* statement) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec(statement);
            return true;
        });
    };
    sql("CREATE OR REPLACE FUNCTION test_sync_claim_fails() RETURNS trigger AS $$ BEGIN "
        "IF NEW.requested_start = DATE '1999-01-01' AND NEW.status = 'running' THEN "
        "RAISE EXCEPTION 'claim refused by the test'; END IF; RETURN NEW; END $$ LANGUAGE plpgsql");
    sql("DROP TRIGGER IF EXISTS test_sync_claim_fails ON sync_runs");
    sql("CREATE TRIGGER test_sync_claim_fails BEFORE UPDATE ON sync_runs "
        "FOR EACH ROW EXECUTE FUNCTION test_sync_claim_fails()");

    auto svc = service();
    EXPECT_ANY_THROW(svc.run(id, "1999-01-01", "1999-01-01", {"daily_activity"}));

    sql("DROP TRIGGER IF EXISTS test_sync_claim_fails ON sync_runs");
    sql("DROP FUNCTION IF EXISTS test_sync_claim_fails()");
    EXPECT_EQ((*runs.get(id, kAccount))["status"], "queued");
    EXPECT_TRUE(transport.requests().empty());
}

TEST_F(SyncServiceTest, LoginOutcomeIsReported) {
    Repositories::SyncRunRepository runs;
    auto accepted = service();
    transport.reply_login();
    transport.reply_encrypted(steps_page(100));
    transport.reply_encrypted(empty_page());
    accepted.run(runs.create(kAccount, "2026-09-22", "2026-09-22", {"daily_activity"}),
                 "2026-09-22",
                 "2026-09-22",
                 {"daily_activity"});
    EXPECT_EQ(accepted.login_outcome(), Sync::SyncService::Login::Accepted);

    auto refused = service();
    transport.reply({200, "no start prefix at all", {}});
    refused.run(runs.create(kAccount, "2026-09-22", "2026-09-22", {"daily_activity"}),
                "2026-09-22",
                "2026-09-22",
                {"daily_activity"});
    EXPECT_EQ(refused.login_outcome(), Sync::SyncService::Login::Refused);
}
