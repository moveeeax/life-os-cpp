/**
 * @file test_fitness_api.cpp
 * @brief Fitness module routes: probe against a fake transport, sync
 *        scheduling, data reads, permissions and the disabled module.
 */

#include <cstdint>
#include <string>
#include <utility>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/FitnessController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "fitness/FakeHttpTransport.hpp"
#include "fitness/xiaomi/Service.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/fitness/ActivityRepository.hpp"
#include "repositories/fitness/MiAccountRepository.hpp"
#include "repositories/fitness/SamplesRepository.hpp"
#include "repositories/fitness/SleepRepository.hpp"
#include "repositories/fitness/SyncRunRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kTestKeyB64 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
// The principal of the tests is a user row: a Mi account links to a user.
constexpr const char* kTestUserId = "77777777-7777-4777-8777-777777777777";

Security::Auth::AuthPrincipal principal_with(std::uint32_t permissions) {
    Security::Auth::AuthPrincipal p;
    p.subject = kTestUserId;
    p.raw_claims = json{{"sub", p.subject}, {"permissions", permissions}};
    return p;
}

Security::Auth::AuthPrincipal reader() {
    return principal_with(Domain::Permission::kGeneral | Domain::Permission::kFitnessRead);
}

Security::Auth::AuthPrincipal operator_() {
    return principal_with(Domain::Permission::kGeneral | Domain::Permission::kFitnessRead |
                          Domain::Permission::kFitnessSync);
}

Security::Auth::AuthPrincipal plain_user() {
    return principal_with(Domain::Permission::kGeneral);
}

json body_of(const HttpResponsePtr& resp) {
    return json::parse(std::string(resp->body()));
}

}  // namespace

// ── probe ────────────────────────────────────────────────────────────────────

namespace {

class XiaomiProbeTest : public TestHelpers::CoreBackedTest {
protected:
    FakeHttpTransport transport;
    Api::FitnessController controller;

    std::string config_file_name() const override { return "xiaomi_probe_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override {
        cfg["fitness"]["enabled"] = true;
        cfg["fitness"]["xiaomi"]["token_key"] = kTestKeyB64;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Xiaomi::Service::install_for_testing(&transport);
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE mi_accounts");
            txn.exec_params("INSERT INTO users (id, email, confirmed, role_id) "
                            "VALUES ($1::uuid, 'fitness-test@example.test', TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                            "ON CONFLICT DO NOTHING",
                            std::string(kTestUserId));
            return true;
        });
    }

    void TearDown() override {
        Xiaomi::Service::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    void seed_credentials() {
        Repositories::MiAccountRepository(kTestKeyB64).link(kTestUserId, {"1234567890", std::string(347, 'S'), "cn"}, true);
    }

    HttpResponsePtr probe(const std::string& key, const std::string& from, const std::string& to) {
        auto request = TestHelpers::authed(operator_(), Get);
        request->setParameter("key", key);
        request->setParameter("from", from);
        request->setParameter("to", to);
        HttpResponsePtr captured;
        controller.probe(request, [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }
};

}  // namespace

TEST_F(XiaomiProbeTest, ReturnsMaskedAccountAndCount) {
    seed_credentials();
    transport.reply_login();
    transport.reply_encrypted(R"({"code":0,"result":{"data_list":[{"a":1},{"a":2}],"has_more":false}})");

    auto resp = probe("steps", "2026-09-22", "2026-09-22");

    ASSERT_NE(resp, nullptr);
    ASSERT_EQ(resp->statusCode(), k200OK);
    const auto body = json::parse(std::string(resp->body()));
    // contains() before reading: on a const JSON, accessing a missing key is
    // an assert inside nlohmann that takes down the whole test binary.
    ASSERT_TRUE(body.contains("data")) << body.dump();
    const auto& data = body["data"];
    EXPECT_EQ(data.value("account", ""), "******90");
    EXPECT_EQ(data.value("region", ""), "cn");
    EXPECT_EQ(data.value("key", ""), "steps");
    EXPECT_EQ(data.value("records", -1), 2);
    // The full account identifier never leaves the service.
    EXPECT_EQ(std::string(resp->body()).find("1234567890"), std::string::npos);
}

TEST_F(XiaomiProbeTest, RejectsUnknownKey) {
    seed_credentials();
    auto resp = probe("nonsense", "2026-09-22", "2026-09-22");
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
    EXPECT_TRUE(transport.requests().empty()) << "must not reach the cloud";
}

TEST_F(XiaomiProbeTest, RejectsReversedRange) {
    seed_credentials();
    auto resp = probe("steps", "2026-09-23", "2026-09-22");
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
    EXPECT_TRUE(transport.requests().empty());
}

TEST_F(XiaomiProbeTest, RejectsMalformedDate) {
    seed_credentials();
    auto resp = probe("steps", "22.09.2026", "2026-09-22");
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
}

// No linked account: the caller's state, not a configuration or cloud error.
TEST_F(XiaomiProbeTest, WithoutALinkedAccountReportsNotLinked) {
    auto resp = probe("steps", "2026-09-22", "2026-09-22");
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k409Conflict);
    EXPECT_EQ(json::parse(std::string(resp->body()))["error"], "not_linked");
    EXPECT_TRUE(transport.requests().empty()) << "must not reach the cloud";
}

// The cloud rejected the stored token: the upstream_auth code is distinct from
// other failures because only a fresh token fixes it, not a retry.
TEST_F(XiaomiProbeTest, UpstreamAuthFailureIsReported) {
    seed_credentials();
    transport.reply({200, "no start prefix at all", {}});

    auto resp = probe("steps", "2026-09-22", "2026-09-22");

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k503ServiceUnavailable);
    EXPECT_EQ(json::parse(std::string(resp->body()))["error"], "upstream_auth");
    // The link now asks to be made again, and the next accepted login clears that.
    Repositories::MiAccountRepository accounts(kTestKeyB64);
    EXPECT_EQ((*accounts.status(kTestUserId))["status"], "reauth_required");
    EXPECT_EQ((*accounts.status(kTestUserId))["last_error"], "upstream_auth");

    transport.reply_login();
    transport.reply_encrypted(R"({"code":0,"result":{"data_list":[],"has_more":false}})");
    ASSERT_EQ(probe("steps", "2026-09-22", "2026-09-22")->statusCode(), k200OK);
    EXPECT_EQ((*accounts.status(kTestUserId))["status"], "ok");
    EXPECT_TRUE((*accounts.status(kTestUserId))["last_error"].is_null());
}

// Phase 3 Global Constraint: probe and sync never run at the same time, both
// log in, and login rotates the token. While a running row is alive, probe
// answers 409 without going to the cloud.
TEST_F(XiaomiProbeTest, ProbeRefusesWhileASyncRunIsRunning) {
    Database::get().execute_write([](auto& txn) {
        txn.exec(
            "INSERT INTO sync_runs (status, requested_start, requested_end, data_types) "
            "VALUES ('running', '2026-09-01', '2026-09-02', '{daily_activity}')");
        return true;
    });

    const auto resp = probe("steps", "2026-09-22", "2026-09-22");

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k409Conflict) << resp->body();
    const auto body = nlohmann::json::parse(std::string(resp->body()));
    EXPECT_EQ(body["error"], "sync_in_progress");
    // Never reached the cloud: no requests in the transport.
    EXPECT_TRUE(transport.requests().empty());
    Database::get().execute_write([](auto& txn) {
        txn.exec("DELETE FROM sync_runs WHERE requested_start = '2026-09-01'");
        return true;
    });
}

// ── sync ─────────────────────────────────────────────────────────────────────

namespace {

class SyncApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::FitnessController controller;

    std::string config_file_name() const override { return "sync_api_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override {
        // The job queue is disabled by default in the minimal test config.
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

    HttpResponsePtr post_sync(const json& body) {
        HttpResponsePtr captured;
        controller.syncEnqueue(TestHelpers::authed_json(operator_(), body),
                               [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    HttpResponsePtr get_status(const std::string& id) {
        HttpResponsePtr captured;
        controller.syncStatus(
            TestHelpers::authed(operator_(), Get), [&](const HttpResponsePtr& r) { captured = r; }, id);
        return captured;
    }
};

}  // namespace

TEST_F(SyncApiTest, EnqueueCreatesARunAndAJob) {
    auto resp = post_sync({{"from", "2026-09-22"}, {"to", "2026-09-23"}});

    ASSERT_NE(resp, nullptr);
    ASSERT_EQ(resp->statusCode(), k202Accepted);
    const auto body = json::parse(std::string(resp->body()));
    ASSERT_TRUE(body.contains("data")) << body.dump();
    const long run_id = body["data"]["run_id"].get<long>();
    EXPECT_EQ(body["data"]["status"], "queued");

    // The run log row exists and carries the range.
    Repositories::SyncRunRepository runs;
    const auto row = runs.get(run_id);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["status"], "queued");
    EXPECT_EQ((*row)["requested_start"], "2026-09-22");

    // The job sits in the queue with the same run_id.
    auto job = Jobs::get().pick({"fitness_sync"}, 1);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->payload["run_id"].get<long>(), run_id);
}

TEST_F(SyncApiTest, EnqueueRejectsMalformedRange) {
    EXPECT_EQ(post_sync({{"from", "22.09.2026"}, {"to", "2026-09-23"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(post_sync({{"from", "2026-09-24"}, {"to", "2026-09-23"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(post_sync(json::object())->statusCode(), k400BadRequest);
}

TEST_F(SyncApiTest, EnqueueRejectsUnknownDataType) {
    auto resp = post_sync({{"from", "2026-09-22"}, {"to", "2026-09-23"}, {"data_types", {"nonsense"}}});
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
}

TEST_F(SyncApiTest, StatusReturnsTheJournalEntry) {
    Repositories::SyncRunRepository runs;
    const long id = runs.create("2026-09-01", "2026-09-07", {"sleep"});
    runs.finish(id, "succeeded", json{{"sleep", {{"added", 5}}}});

    auto resp = get_status(std::to_string(id));
    ASSERT_NE(resp, nullptr);
    ASSERT_EQ(resp->statusCode(), k200OK);
    const auto body = json::parse(std::string(resp->body()));
    ASSERT_TRUE(body.contains("data")) << body.dump();
    EXPECT_EQ(body["data"]["status"], "succeeded");
    EXPECT_EQ(body["data"]["result"]["sleep"]["added"], 5);
}

TEST_F(SyncApiTest, StatusRejectsUnknownAndMalformedIds) {
    EXPECT_EQ(get_status("999999")->statusCode(), k404NotFound);
    EXPECT_EQ(get_status("not-a-number")->statusCode(), k400BadRequest);
}

// Plan 2 review, Minor: a duplicate in data_types ran the type twice, and the
// second result write overwrote the first.
TEST_F(SyncApiTest, EnqueueRejectsDuplicateDataTypes) {
    const auto resp = post_sync({{"from", "2026-09-22"}, {"to", "2026-09-23"}, {"data_types", {"sleep", "sleep"}}});
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
    const auto body = json::parse(std::string(resp->body()));
    EXPECT_EQ(body["error"], "duplicate_data_type");
}

// ── reads ────────────────────────────────────────────────────────────────────

namespace {

class DataApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::FitnessController controller;

    std::string config_file_name() const override { return "data_api_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override { cfg["fitness"]["enabled"] = true; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec(
                "TRUNCATE TABLE daily_activity, sleep_sessions, heart_rate_samples, "
                "stress_samples, spo2_samples, body_measurements, workouts");
            return true;
        });
    }

    static HttpRequestPtr ranged(const std::string& from, const std::string& to) {
        auto req = TestHelpers::authed(operator_(), Get);
        req->setParameter("from", from);
        req->setParameter("to", to);
        return req;
    }

    static json body_of(const HttpResponsePtr& resp) { return json::parse(std::string(resp->body())); }

    void seed_activity(const std::string& date, long steps) {
        Domain::DailyActivity d;
        d.user_id = "42";
        d.date = date;
        d.steps = steps;
        d.distance_m = 100.0 * static_cast<double>(steps) / 100.0;
        Repositories::ActivityRepository().upsert({d});
    }

    void seed_heart_rate(const std::string& ts, int bpm, const std::string& type) {
        Domain::HeartRateSample s;
        s.user_id = "42";
        s.timestamp = ts;
        s.bpm = bpm;
        s.sample_type = type;
        Repositories::SamplesRepository().upsert_heart_rate({s});
    }
};

}  // namespace

TEST_F(DataApiTest, DailyActivityReturnsRowsInRange) {
    seed_activity("2026-09-20", 1000);
    seed_activity("2026-09-21", 2000);
    seed_activity("2026-09-25", 3000);

    HttpResponsePtr resp;
    controller.dailyActivity(ranged("2026-09-20", "2026-09-22"), [&](const HttpResponsePtr& r) { resp = r; });

    ASSERT_NE(resp, nullptr);
    ASSERT_EQ(resp->statusCode(), k200OK) << resp->body();
    const auto body = body_of(resp);
    EXPECT_EQ(body["count"], 2);
    EXPECT_EQ(body["total"], 2);
    ASSERT_EQ(body["data"].size(), 2u);
    EXPECT_EQ(body["data"][0]["date"], "2026-09-20");
    EXPECT_EQ(body["data"][0]["steps"], 1000);
    EXPECT_EQ(body["data"][1]["date"], "2026-09-21");
}

TEST_F(DataApiTest, RangeIsValidated) {
    HttpResponsePtr resp;
    controller.dailyActivity(ranged("20.09.2026", "2026-09-22"), [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);

    auto no_to = TestHelpers::authed(operator_(), Get);
    no_to->setParameter("from", "2026-09-20");
    HttpResponsePtr resp2;
    controller.dailyActivity(no_to, [&](const HttpResponsePtr& r) { resp2 = r; });
    ASSERT_NE(resp2, nullptr);
    EXPECT_EQ(resp2->statusCode(), k400BadRequest);
}

TEST_F(DataApiTest, HeartRateFiltersByTypeAndPaginates) {
    seed_heart_rate("2026-09-21T10:00:00+00:00", 70, "passive");
    seed_heart_rate("2026-09-21T11:00:00+00:00", 80, "passive");
    seed_heart_rate("2026-09-21T00:00:00+00:00", 55, "resting");

    HttpResponsePtr by_type;
    auto req = ranged("2026-09-21", "2026-09-21");
    req->setParameter("type", "resting");
    controller.heartRate(req, [&](const HttpResponsePtr& r) { by_type = r; });
    ASSERT_NE(by_type, nullptr);
    ASSERT_EQ(by_type->statusCode(), k200OK) << by_type->body();
    auto body = body_of(by_type);
    ASSERT_EQ(body["data"].size(), 1u);
    EXPECT_EQ(body["data"][0]["bpm"], 55);

    HttpResponsePtr page;
    auto preq = ranged("2026-09-21", "2026-09-21");
    preq->setParameter("limit", "1");
    preq->setParameter("offset", "1");
    controller.heartRate(preq, [&](const HttpResponsePtr& r) { page = r; });
    ASSERT_NE(page, nullptr);
    body = body_of(page);
    EXPECT_EQ(body["total"], 3);
    EXPECT_EQ(body["count"], 1);
    ASSERT_EQ(body["data"].size(), 1u);
    // Sorted by time: the second row is 10:00.
    EXPECT_EQ(body["data"][0]["bpm"], 70);
}

TEST_F(DataApiTest, SleepRowsCarrySessionFields) {
    Domain::SleepSession s;
    s.user_id = "42";
    s.sleep_id = "sess-1";
    s.start_at = "2026-09-20T23:00:00+00:00";
    s.end_at = "2026-09-21T06:30:00+00:00";
    s.start_epoch = 1789858800;
    s.end_epoch = 1789885800;
    s.duration_minutes = 450;
    s.time_asleep_minutes = 430;
    s.time_awake_minutes = 20;
    s.sleep_score = 77;
    s.sleep_score_source = "daily_report";
    Repositories::SleepRepository().upsert({s});

    HttpResponsePtr resp;
    controller.sleep(ranged("2026-09-21", "2026-09-21"), [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    ASSERT_EQ(resp->statusCode(), k200OK) << resp->body();
    const auto body = body_of(resp);
    ASSERT_EQ(body["data"].size(), 1u);
    const auto& row = body["data"][0];
    EXPECT_EQ(row["sleep_id"], "sess-1");
    EXPECT_EQ(row["duration_minutes"], 450);
    EXPECT_EQ(row["sleep_score"], 77);
    EXPECT_EQ(row["sleep_score_source"], "daily_report");
    EXPECT_EQ(row["is_nap"], false);
}

TEST_F(DataApiTest, SummaryJoinsTheDay) {
    seed_activity("2026-09-21", 5000);
    seed_heart_rate("2026-09-21T00:00:00+00:00", 55, "resting");
    Domain::SleepSession s;
    s.user_id = "42";
    s.sleep_id = "sess-2";
    s.start_at = "2026-09-20T23:00:00+00:00";
    s.end_at = "2026-09-21T06:30:00+00:00";
    s.start_epoch = 1789858800;
    s.end_epoch = 1789885800;
    s.duration_minutes = 450;
    s.time_asleep_minutes = 430;
    s.time_awake_minutes = 20;
    s.sleep_score = 77;
    Repositories::SleepRepository().upsert({s});

    HttpResponsePtr resp;
    controller.summary(ranged("2026-09-21", "2026-09-21"), [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    ASSERT_EQ(resp->statusCode(), k200OK) << resp->body();
    const auto body = body_of(resp);
    ASSERT_EQ(body["data"].size(), 1u);
    const auto& row = body["data"][0];
    EXPECT_EQ(row["date"], "2026-09-21");
    EXPECT_EQ(row["steps"], 5000);
    EXPECT_EQ(row["sleep_duration_minutes"], 450);
    EXPECT_EQ(row["sleep_score"], 77);
    EXPECT_EQ(row["resting_bpm"], 55);
}

TEST_F(DataApiTest, CoverageReportsPerTypeBounds) {
    seed_activity("2026-09-20", 1000);
    seed_activity("2026-09-25", 3000);
    seed_heart_rate("2026-09-21T10:00:00+00:00", 70, "passive");

    HttpResponsePtr resp;
    controller.coverage(TestHelpers::authed(operator_(), Get), [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    ASSERT_EQ(resp->statusCode(), k200OK) << resp->body();
    const auto body = body_of(resp);
    const auto& act = body["data"]["daily_activity"];
    EXPECT_EQ(act["first_date"], "2026-09-20");
    EXPECT_EQ(act["last_date"], "2026-09-25");
    EXPECT_EQ(act["records"], 2);
    const auto& hr = body["data"]["heart_rate"];
    EXPECT_EQ(hr["records"], 1);
    // An empty type means NULL bounds, not garbage.
    EXPECT_TRUE(body["data"]["workouts"]["first_date"].is_null());
    EXPECT_EQ(body["data"]["workouts"]["records"], 0);
}

TEST_F(DataApiTest, ExportJsonCarriesTheBridgeEnvelope) {
    seed_activity("2026-09-21", 5000);
    seed_heart_rate("2026-09-21T10:00:00+00:00", 70, "passive");

    auto req = ranged("2026-09-21", "2026-09-21");
    req->setParameter("format", "json");
    HttpResponsePtr resp;
    controller.exportData(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    ASSERT_EQ(resp->statusCode(), k200OK) << resp->body();
    const auto body = body_of(resp);
    EXPECT_EQ(body["schema_version"], "1.0");
    EXPECT_EQ(body["source"], "life-os-cpp");
    EXPECT_EQ(body["filters"]["start_date"], "2026-09-21");
    ASSERT_TRUE(body["records"].contains("daily_activity"));
    EXPECT_EQ(body["records"]["daily_activity"].size(), 1u);
    EXPECT_EQ(body["records"]["heart_rate"].size(), 1u);
}

TEST_F(DataApiTest, ExportCsvNeedsATypeAndEscapesFormulas) {
    Domain::DailyActivity d;
    d.user_id = "42";
    d.date = "2026-09-21";
    d.steps = 5000;
    d.timezone = "=SUM(A1:A9)";  // a formula in a string field must not survive
    Repositories::ActivityRepository().upsert({d});

    auto no_type = ranged("2026-09-21", "2026-09-21");
    no_type->setParameter("format", "csv");
    HttpResponsePtr bad;
    controller.exportData(no_type, [&](const HttpResponsePtr& r) { bad = r; });
    ASSERT_NE(bad, nullptr);
    EXPECT_EQ(bad->statusCode(), k400BadRequest);

    auto req = ranged("2026-09-21", "2026-09-21");
    req->setParameter("format", "csv");
    req->setParameter("type", "daily_activity");
    HttpResponsePtr resp;
    controller.exportData(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    ASSERT_EQ(resp->statusCode(), k200OK) << resp->body();
    const std::string body(resp->body());
    EXPECT_NE(body.find("date"), std::string::npos);
    EXPECT_NE(body.find("5000"), std::string::npos);
    // The leading equals sign is escaped with an apostrophe.
    EXPECT_EQ(body.find(",=SUM"), std::string::npos);
    EXPECT_NE(body.find("'=SUM"), std::string::npos);
}

// Phase 3 review, Important 3: abnormal heart rate events were synced but
// never read or exported, and the bridge that exported them is gone.
TEST_F(DataApiTest, AbnormalHeartBeatIsReadableAndExported) {
    Database::get().execute_write([](auto& txn) {
        txn.exec(
            "INSERT INTO abnormal_heart_beat_events (user_id, event_id, start_at, end_at, duration_seconds) "
            "VALUES ('42', 'ev-1', '2026-09-21T10:00:00+00:00', '2026-09-21T10:01:00+00:00', 60)");
        return true;
    });

    HttpResponsePtr resp;
    controller.abnormalHeartBeat(ranged("2026-09-21", "2026-09-21"), [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    ASSERT_EQ(resp->statusCode(), k200OK) << resp->body();
    auto body = body_of(resp);
    ASSERT_EQ(body["data"].size(), 1u);
    EXPECT_EQ(body["data"][0]["event_id"], "ev-1");

    auto req = ranged("2026-09-21", "2026-09-21");
    req->setParameter("format", "json");
    HttpResponsePtr exp;
    controller.exportData(req, [&](const HttpResponsePtr& r) { exp = r; });
    ASSERT_NE(exp, nullptr);
    const auto envelope = body_of(exp);
    ASSERT_TRUE(envelope["records"].contains("abnormal_heart_beat")) << envelope.dump();
    EXPECT_EQ(envelope["records"]["abnormal_heart_beat"].size(), 1u);
}

// Phase 3 review, Important 5: the activity date is local (device timezone),
// but summary windows were built from UTC days, so the morning resting heart
// rate landed in the previous row. The window is computed in the region
// timezone (+08 for cn).
TEST_F(DataApiTest, SummaryWindowFollowsTheRegionZone) {
    seed_activity("2026-09-21", 5000);
    // 05:00 +08:00 on the 21st = 21:00Z on the 20th: in a UTC window that is
    // the previous day.
    seed_heart_rate("2026-09-20T21:00:00+00:00", 55, "resting");

    HttpResponsePtr resp;
    controller.summary(ranged("2026-09-21", "2026-09-21"), [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    ASSERT_EQ(resp->statusCode(), k200OK) << resp->body();
    const auto body = body_of(resp);
    ASSERT_EQ(body["data"].size(), 1u);
    EXPECT_EQ(body["data"][0]["resting_bpm"], 55);
}

// Phase 3 review, Important 8: export without a cap on the window width
// pulled years of data into memory as a single json_agg value.
TEST_F(DataApiTest, ExportRejectsARangeWiderThanAYear) {
    auto req = ranged("2020-01-01", "2030-01-01");
    req->setParameter("format", "json");
    HttpResponsePtr resp;
    controller.exportData(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(resp)["error"], "range_too_wide");
}

// ── permissions and module switch ────────────────────────────────────────────

namespace {

class FitnessGuardsTest : public TestHelpers::CoreBackedTest {
protected:
    Api::FitnessController controller;

    std::string config_file_name() const override { return "fitness_guards_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override { cfg["fitness"]["enabled"] = true; }

    HttpResponsePtr sleep_as(const Security::Auth::AuthPrincipal& p) {
        auto req = TestHelpers::authed(p, Get);
        req->setParameter("from", "2026-09-01");
        req->setParameter("to", "2026-09-02");
        HttpResponsePtr captured;
        controller.sleep(req, [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    HttpResponsePtr probe_as(const Security::Auth::AuthPrincipal& p) {
        auto req = TestHelpers::authed(p, Get);
        req->setParameter("key", "heart_rate");
        req->setParameter("from", "2026-09-01");
        req->setParameter("to", "2026-09-02");
        HttpResponsePtr captured;
        controller.probe(req, [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }
};

class FitnessDisabledTest : public TestHelpers::CoreBackedTest {
protected:
    Api::FitnessController controller;

    std::string config_file_name() const override { return "fitness_disabled_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override { cfg["fitness"]["enabled"] = false; }
};

}  // namespace

TEST_F(FitnessGuardsTest, PlainUserGets403OnRead) {
    // The User role (0x01) without the kFitnessRead bit.
    const auto resp = sleep_as(plain_user());
    ASSERT_TRUE(resp);
    EXPECT_EQ(resp->statusCode(), k403Forbidden);
    EXPECT_EQ(body_of(resp)["error"], "forbidden");
    EXPECT_EQ(body_of(resp)["required_permission"], Domain::Permission::kFitnessRead);
}

TEST_F(FitnessGuardsTest, ReaderGets200OnReadAnd403OnProbe) {
    const auto ok = sleep_as(reader());
    ASSERT_TRUE(ok);
    EXPECT_EQ(ok->statusCode(), k200OK);
    const auto denied = probe_as(reader());
    ASSERT_TRUE(denied);
    EXPECT_EQ(denied->statusCode(), k403Forbidden);
}

TEST_F(FitnessGuardsTest, AdminPassesEveryGuard) {
    const auto resp = sleep_as(principal_with(Domain::Permission::kAdminister));
    ASSERT_TRUE(resp);
    EXPECT_EQ(resp->statusCode(), k200OK);
}

TEST_F(FitnessGuardsTest, CsvExportWithoutTypeIs400) {
    auto req = TestHelpers::authed(reader(), Get);
    req->setParameter("from", "2026-09-01");
    req->setParameter("to", "2026-09-02");
    req->setParameter("format", "csv");
    HttpResponsePtr captured;
    controller.exportData(req, [&](const HttpResponsePtr& r) { captured = r; });
    ASSERT_TRUE(captured);
    EXPECT_EQ(captured->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(captured)["error"], "csv_needs_type");
}

TEST_F(FitnessDisabledTest, EveryRouteIs404WhenModuleIsOff) {
    // Even an admin, and even without credentials: the module is disabled.
    auto req = TestHelpers::authed(principal_with(Domain::Permission::kAdminister), Get);
    req->setParameter("from", "2026-09-01");
    req->setParameter("to", "2026-09-02");
    HttpResponsePtr captured;
    controller.sleep(req, [&](const HttpResponsePtr& r) { captured = r; });
    ASSERT_TRUE(captured);
    EXPECT_EQ(captured->statusCode(), k404NotFound);
    EXPECT_EQ(body_of(captured)["error"], "not_found");
    EXPECT_EQ(body_of(captured)["message"], "fitness not found");

    captured.reset();
    controller.coverage(TestHelpers::authed(principal_with(Domain::Permission::kAdminister), Get),
                        [&](const HttpResponsePtr& r) { captured = r; });
    ASSERT_TRUE(captured);
    EXPECT_EQ(captured->statusCode(), k404NotFound);
}
