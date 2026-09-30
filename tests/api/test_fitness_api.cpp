/**
 * @file test_fitness_api.cpp
 * @brief Маршруты модуля fitness: probe с подделкой транспорта, постановка
 *        синка, чтение данных, права и выключенный модуль.
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
#include "repositories/fitness/CredentialsRepository.hpp"
#include "repositories/fitness/SamplesRepository.hpp"
#include "repositories/fitness/SleepRepository.hpp"
#include "repositories/fitness/SyncRunRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kTestKeyB64 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";

Security::Auth::AuthPrincipal principal_with(std::uint32_t permissions) {
    Security::Auth::AuthPrincipal p;
    p.subject = "fitness-test-user";
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
            txn.exec("TRUNCATE TABLE xiaomi_credentials");
            return true;
        });
    }

    void TearDown() override {
        Xiaomi::Service::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    void seed_credentials() {
        Repositories::CredentialsRepository(kTestKeyB64).store({"1234567890", std::string(347, 'S'), "cn"});
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
    // contains до чтения: на const JSON доступ по отсутствующему ключу это
    // assert внутри nlohmann, который валит весь тестовый бинарь.
    ASSERT_TRUE(body.contains("data")) << body.dump();
    const auto& data = body["data"];
    EXPECT_EQ(data.value("account", ""), "******90");
    EXPECT_EQ(data.value("region", ""), "cn");
    EXPECT_EQ(data.value("key", ""), "steps");
    EXPECT_EQ(data.value("records", -1), 2);
    // Полный идентификатор аккаунта наружу не выходит.
    EXPECT_EQ(std::string(resp->body()).find("1234567890"), std::string::npos);
}

TEST_F(XiaomiProbeTest, RejectsUnknownKey) {
    seed_credentials();
    auto resp = probe("nonsense", "2026-09-22", "2026-09-22");
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
    EXPECT_TRUE(transport.requests().empty()) << "до облака дойти не должно";
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

// Учётные данные ещё не посеяны: это состояние конфигурации, а не ошибка
// клиента и не ошибка облака.
TEST_F(XiaomiProbeTest, WithoutCredentialsReportsNotConfigured) {
    auto resp = probe("steps", "2026-09-22", "2026-09-22");
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k503ServiceUnavailable);
    EXPECT_EQ(json::parse(std::string(resp->body()))["error"], "not_configured");
}

// Облако отвергло сохранённый токен: код upstream_auth отличим от прочих
// отказов, потому что лечится только свежим токеном, а не повтором.
TEST_F(XiaomiProbeTest, UpstreamAuthFailureIsReported) {
    seed_credentials();
    transport.reply({200, "no start prefix at all", {}});

    auto resp = probe("steps", "2026-09-22", "2026-09-22");

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k503ServiceUnavailable);
    EXPECT_EQ(json::parse(std::string(resp->body()))["error"], "upstream_auth");
}

// Global Constraint фазы 3: probe и синк не гоняют одновременно — оба
// логинятся, а логин ротирует токен. При живой строке running probe
// отвечает 409 без похода в облако.
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
    // До облака не дошли: ни одного запроса в транспорте.
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
        // Очередь заданий по умолчанию выключена в минимальном конфиге тестов.
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

    // Строка журнала существует и несёт диапазон.
    Repositories::SyncRunRepository runs;
    const auto row = runs.get(run_id);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ((*row)["status"], "queued");
    EXPECT_EQ((*row)["requested_start"], "2026-09-22");

    // Задание лежит в очереди с тем же run_id.
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

// Minor обзора плана 2: дубль в data_types прогонял тип дважды, вторая
// запись результата затирала первую.
TEST_F(SyncApiTest, EnqueueRejectsDuplicateDataTypes) {
    const auto resp = post_sync({{"from", "2026-09-22"}, {"to", "2026-09-23"}, {"data_types", {"sleep", "sleep"}}});
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
    const auto body = json::parse(std::string(resp->body()));
    EXPECT_EQ(body["error"], "duplicate_data_type");
}

// ── чтение ───────────────────────────────────────────────────────────────────

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
    // Сортировка по времени: вторая строка это 10:00.
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
    // Пустой тип это NULL-границы, не мусор.
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
    EXPECT_EQ(body["source"], "mi-fitness-api");
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
    d.timezone = "=SUM(A1:A9)";  // формула в строковом поле не должна выжить
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
    // Ведущий знак равенства экранирован апострофом.
    EXPECT_EQ(body.find(",=SUM"), std::string::npos);
    EXPECT_NE(body.find("'=SUM"), std::string::npos);
}

// Обзор фазы 3, Important 3: события аномального пульса синкаются, но не
// читались и не выгружались, а мост, который их выгружал, удалён.
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

// Обзор фазы 3, Important 5: date у активности локальная (пояс устройства),
// а окна сводки строились от суток UTC — утренний resting-пульс уезжал в
// предыдущую строку. Окно считается в поясе региона (+08 для cn).
TEST_F(DataApiTest, SummaryWindowFollowsTheRegionZone) {
    seed_activity("2026-09-21", 5000);
    // 05:00 +08:00 двадцать первого = 21:00Z двадцатого: по UTC-окну это
    // предыдущие сутки.
    seed_heart_rate("2026-09-20T21:00:00+00:00", 55, "resting");

    HttpResponsePtr resp;
    controller.summary(ranged("2026-09-21", "2026-09-21"), [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    ASSERT_EQ(resp->statusCode(), k200OK) << resp->body();
    const auto body = body_of(resp);
    ASSERT_EQ(body["data"].size(), 1u);
    EXPECT_EQ(body["data"][0]["resting_bpm"], 55);
}

// Обзор фазы 3, Important 8: экспорт без потолка ширины окна собирал в
// память годы данных одним значением json_agg.
TEST_F(DataApiTest, ExportRejectsARangeWiderThanAYear) {
    auto req = ranged("2020-01-01", "2030-01-01");
    req->setParameter("format", "json");
    HttpResponsePtr resp;
    controller.exportData(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(resp)["error"], "range_too_wide");
}

// ── права и выключатель модуля ───────────────────────────────────────────────

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
    // Роль User (0x01) без бита kFitnessRead.
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
    // Даже администратор и даже без учётных данных: модуль выключен.
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
