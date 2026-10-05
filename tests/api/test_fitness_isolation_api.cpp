/**
 * @file test_fitness_isolation_api.cpp
 * @brief Fitness data belongs to the user whose Mi account it came from: no
 *        read route returns a row of another user's account.
 */

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/FitnessController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa3";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb3";
constexpr const char* kVera = "cccccccc-cccc-4ccc-8ccc-ccccccccccc3";  // no linked account
// Marker values: every row of Anna's account carries 111, of Boris's 222.
constexpr const char* kAnnaAccount = "9000000111";
constexpr const char* kBorisAccount = "9000000222";

Security::Auth::AuthPrincipal user(const std::string& id, std::uint32_t permissions) {
    Security::Auth::AuthPrincipal p;
    p.subject = id;
    p.raw_claims = json{{"sub", id}, {"permissions", permissions}};
    return p;
}

Security::Auth::AuthPrincipal reader(const std::string& id) {
    return user(id, Domain::Permission::kGeneral | Domain::Permission::kFitnessRead | Domain::Permission::kFitnessSync);
}

class FitnessIsolationTest : public TestHelpers::CoreBackedTest {
protected:
    Api::FitnessController controller;

    using Route = void (Api::FitnessController::*)(const HttpRequestPtr&,
                                                   std::function<void(const HttpResponsePtr&)>&&);

    std::string config_file_name() const override { return "fitness_isolation_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override { cfg["fitness"]["enabled"] = true; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec(
                "TRUNCATE TABLE daily_activity, sleep_sessions, heart_rate_samples, stress_samples, "
                "spo2_samples, body_measurements, workouts, abnormal_heart_beat_events, sync_runs, "
                "sync_state, mi_accounts");
            for (const char* id : {kAnna, kBoris, kVera}) {
                txn.exec_params(
                    "INSERT INTO users (id, email, confirmed, role_id) "
                    "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                    "ON CONFLICT DO NOTHING",
                    std::string(id),
                    std::string(id) + "@example.test");
            }
            txn.exec_params(
                "INSERT INTO mi_accounts (owner_id, xiaomi_user_id, pass_token_sealed, nonce, region) "
                "VALUES ($1::uuid, $3, 'x', 'x', 'cn'), ($2::uuid, $4, 'x', 'x', 'cn')",
                std::string(kAnna),
                std::string(kBoris),
                std::string(kAnnaAccount),
                std::string(kBorisAccount));
            // The same day and the same instants for both accounts, different values.
            for (const auto& [account, n] : {std::pair<const char*, int>{kAnnaAccount, 111}, {kBorisAccount, 222}}) {
                const std::string a(account);
                const std::string v = std::to_string(n);
                txn.exec("INSERT INTO daily_activity (user_id, date, steps) VALUES ('" + a + "', '2026-09-21', " + v +
                         ")");
                txn.exec(
                    "INSERT INTO sleep_sessions (user_id, sleep_id, start_at, end_at, duration_minutes, "
                    "time_asleep_minutes, time_awake_minutes, sleep_score) VALUES ('" +
                    a + "', 'night-" + v + "', '2026-09-20T15:00:00Z', '2026-09-20T23:00:00Z', " + v + ", " + v +
                    ", 0, 80)");
                txn.exec("INSERT INTO heart_rate_samples (user_id, timestamp, bpm, sample_type) VALUES ('" + a +
                         "', '2026-09-21T10:00:00Z', " + v + ", 'passive'), ('" + a + "', '2026-09-21T01:00:00Z', " +
                         v + ", 'resting')");
                txn.exec("INSERT INTO stress_samples (user_id, timestamp, stress_score, level) VALUES ('" + a +
                         "', '2026-09-21T10:00:00Z', " + v + ", 'low')");
                txn.exec("INSERT INTO spo2_samples (user_id, timestamp, spo2_pct) VALUES ('" + a +
                         "', '2026-09-21T10:00:00Z', " + v + ")");
                txn.exec("INSERT INTO body_measurements (user_id, timestamp, weight_kg) VALUES ('" + a +
                         "', '2026-09-21T10:00:00Z', " + v + ")");
                txn.exec(
                    "INSERT INTO workouts (user_id, workout_id, activity_type, start_at, end_at, duration_minutes) "
                    "VALUES ('" +
                    a + "', 'w-" + v + "', 'strength', '2026-09-21T10:00:00Z', '2026-09-21T11:00:00Z', " + v + ")");
                txn.exec(
                    "INSERT INTO abnormal_heart_beat_events (user_id, event_id, start_at, end_at, duration_seconds) "
                    "VALUES ('" +
                    a + "', 'ev-" + v + "', '2026-09-21T10:00:00Z', '2026-09-21T10:01:00Z', " + v + ")");
                txn.exec("INSERT INTO sync_state (xiaomi_user_id, data_type, last_sync_at, records_count) VALUES ('" +
                         a + "', 'sleep', '2026-09-21T12:00:00Z', " + v + ")");
            }
            return true;
        });
    }

    json read(Route route, const std::string& who, const std::vector<std::pair<std::string, std::string>>& extra = {}) {
        auto req = TestHelpers::authed(reader(who), Get);
        req->setParameter("from", "2026-09-20");
        req->setParameter("to", "2026-09-22");
        for (const auto& [k, v] : extra) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        (controller.*route)(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        EXPECT_EQ(captured->statusCode(), k200OK) << captured->body();
        return json::parse(std::string(captured->body()));
    }
};

struct PagedRoute {
    const char* name;
    void (Api::FitnessController::*route)(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&);
    const char* field;  // the field that carries the marker value
    std::size_t rows;   // rows of one account in the range
};

const PagedRoute kPagedRoutes[] = {
    {"daily-activity", &Api::FitnessController::dailyActivity, "steps", 1},
    {"sleep", &Api::FitnessController::sleep, "duration_minutes", 1},
    {"heart-rate", &Api::FitnessController::heartRate, "bpm", 2},
    {"stress", &Api::FitnessController::stress, "stress_score", 1},
    {"spo2", &Api::FitnessController::spo2, "spo2_pct", 1},
    {"body", &Api::FitnessController::body, "weight_kg", 1},
    {"workouts", &Api::FitnessController::workouts, "duration_minutes", 1},
    {"abnormal-heart-beat", &Api::FitnessController::abnormalHeartBeat, "duration_seconds", 1},
    {"summary", &Api::FitnessController::summary, "steps", 1},
};

}  // namespace

TEST_F(FitnessIsolationTest, EveryPagedRouteReturnsOnlyTheCallersRows) {
    for (const auto& r : kPagedRoutes) {
        SCOPED_TRACE(r.name);
        for (const auto& [who, marker] : {std::pair<const char*, int>{kAnna, 111}, {kBoris, 222}}) {
            const json body = read(r.route, who);
            EXPECT_EQ(body["total"], r.rows);
            ASSERT_EQ(body["data"].size(), r.rows);
            for (const auto& row : body["data"]) {
                EXPECT_EQ(row[r.field].get<double>(), marker) << row.dump();
            }
        }
        // A user without a linked account has no rows; it is not an error.
        const json nothing = read(r.route, kVera);
        EXPECT_EQ(nothing["total"], 0);
        EXPECT_TRUE(nothing["data"].empty());
    }
}

TEST_F(FitnessIsolationTest, SummaryJoinsOnlyTheCallersSleepAndHeartRate) {
    const json anna = read(&Api::FitnessController::summary, kAnna)["data"][0];
    EXPECT_EQ(anna["steps"], 111);
    EXPECT_EQ(anna["sleep_duration_minutes"], 111);
    EXPECT_EQ(anna["resting_bpm"], 111);

    // Boris without his own sleep and resting samples gets nulls, not Anna's.
    Database::get().execute_write([](auto& txn) {
        txn.exec(std::string("DELETE FROM sleep_sessions WHERE user_id = '") + kBorisAccount + "'");
        txn.exec(std::string("DELETE FROM heart_rate_samples WHERE user_id = '") + kBorisAccount + "'");
        return true;
    });
    const json boris = read(&Api::FitnessController::summary, kBoris)["data"][0];
    EXPECT_EQ(boris["steps"], 222);
    EXPECT_TRUE(boris["sleep_duration_minutes"].is_null());
    EXPECT_TRUE(boris["resting_bpm"].is_null());
}

TEST_F(FitnessIsolationTest, HeartRateTypeFilterStaysInsideTheAccount) {
    const json resting = read(&Api::FitnessController::heartRate, kAnna, {{"type", "resting"}});
    ASSERT_EQ(resting["data"].size(), 1u);
    EXPECT_EQ(resting["data"][0]["bpm"], 111);
    EXPECT_EQ(resting["total"], 1);
}

TEST_F(FitnessIsolationTest, CoverageCountsOnlyTheCallersRows) {
    const auto coverage = [&](const char* who) {
        HttpResponsePtr captured;
        controller.coverage(TestHelpers::authed(reader(who), Get), [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_EQ(captured->statusCode(), k200OK);
        return json::parse(std::string(captured->body()))["data"];
    };
    const json anna = coverage(kAnna);
    EXPECT_EQ(anna["heart_rate"]["records"], 2);
    EXPECT_EQ(anna["daily_activity"]["records"], 1);
    EXPECT_EQ(anna["daily_activity"]["first_date"], "2026-09-21");
    EXPECT_FALSE(anna["sleep"]["last_sync_at"].is_null());

    const json vera = coverage(kVera);
    for (const auto& [type, entry] : vera.items()) {
        EXPECT_EQ(entry["records"], 0) << type;
        EXPECT_TRUE(entry["first_date"].is_null()) << type;
        EXPECT_TRUE(entry["last_sync_at"].is_null()) << type;
    }
}

TEST_F(FitnessIsolationTest, ExportCarriesOnlyTheCallersRows) {
    const auto exported = [&](const char* who) {
        auto req = TestHelpers::authed(reader(who), Get);
        req->setParameter("from", "2026-09-20");
        req->setParameter("to", "2026-09-22");
        HttpResponsePtr captured;
        controller.exportData(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_EQ(captured->statusCode(), k200OK) << captured->body();
        return std::string(captured->body());
    };
    const std::string anna = exported(kAnna);
    EXPECT_NE(anna.find("night-111"), std::string::npos);
    EXPECT_EQ(anna.find("night-222"), std::string::npos);
    EXPECT_EQ(anna.find("w-222"), std::string::npos);
    EXPECT_EQ(anna.find("ev-222"), std::string::npos);

    const std::string vera = exported(kVera);
    EXPECT_EQ(vera.find("night-"), std::string::npos);
    EXPECT_EQ(vera.find("w-1"), std::string::npos);

    // CSV of one type as well.
    auto req = TestHelpers::authed(reader(kBoris), Get);
    req->setParameter("from", "2026-09-20");
    req->setParameter("to", "2026-09-22");
    req->setParameter("format", "csv");
    req->setParameter("type", "sleep");
    HttpResponsePtr csv;
    controller.exportData(req, [&](const HttpResponsePtr& r) { csv = r; });
    ASSERT_EQ(csv->statusCode(), k200OK);
    EXPECT_NE(std::string(csv->body()).find("night-222"), std::string::npos);
    EXPECT_EQ(std::string(csv->body()).find("night-111"), std::string::npos);
}

TEST_F(FitnessIsolationTest, AnAdministratorSeesOnlyTheirOwnAccountToo) {
    // Vera is an administrator here and has no linked account.
    auto req = TestHelpers::authed(user(kVera, Domain::Permission::kAdminister), Get);
    req->setParameter("from", "2026-09-20");
    req->setParameter("to", "2026-09-22");
    HttpResponsePtr captured;
    controller.heartRate(req, [&](const HttpResponsePtr& r) { captured = r; });
    ASSERT_EQ(captured->statusCode(), k200OK);
    EXPECT_EQ(json::parse(std::string(captured->body()))["total"], 0);
}

TEST_F(FitnessIsolationTest, ReadsNeedAUserAccount) {
    auto req = TestHelpers::authed(reader("static-bearer"), Get);
    req->setParameter("from", "2026-09-20");
    req->setParameter("to", "2026-09-22");
    HttpResponsePtr captured;
    controller.sleep(req, [&](const HttpResponsePtr& r) { captured = r; });
    ASSERT_TRUE(captured);
    EXPECT_EQ(captured->statusCode(), k403Forbidden);
    EXPECT_EQ(json::parse(std::string(captured->body()))["error"], "no_user_account");
}
