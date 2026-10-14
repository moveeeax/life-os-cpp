/**
 * @file test_now_settings_api.cpp
 * @brief The Now settings, weather and note routes: the default layout, a
 *        layout with a chip twice refused, the city geocoded through a fake
 *        Open-Meteo, an unknown city 422, the geocoder down 503, the weather
 *        fetched once and then served from the cache, no city no weather, the
 *        note of a day and of the day before in /today.
 */

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/TodayController.hpp"
#include "cache/Cache.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "net/Http.hpp"
#include "now/Weather.hpp"
#include "repositories/now/NoteRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaf1";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbf2";
constexpr const char* kDay = "2026-10-14";

Security::Auth::AuthPrincipal user(const std::string& id) {
    Security::Auth::AuthPrincipal p;
    p.subject = id;
    p.raw_claims = json{{"sub", id}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

json body_of(const HttpResponsePtr& resp) {
    return json::parse(std::string(resp->body()));
}

/// Open-Meteo as the tests want it: a known city, an unknown one, a failing mode.
class FakeOpenMeteo : public Net::Http::Transport {
public:
    std::vector<std::string> urls;
    bool down = false;

    Net::Http::Response get(const std::string& url, const std::vector<std::pair<std::string, std::string>>&) override {
        urls.push_back(url);
        if (down) {
            throw Net::Http::TransportError("timed out");
        }
        if (url.find("geocoding-api") != std::string::npos) {
            if (url.find("name=Almaty") != std::string::npos) {
                return {200,
                        R"({"results":[{"name":"Almaty","latitude":43.25249,"longitude":76.9115,)"
                        R"("country_code":"KZ","timezone":"Asia/Almaty"}]})"};
            }
            return {200, R"({"generationtime_ms":0.3})"};
        }
        return {200,
                R"({"current":{"time":"2026-10-14T06:00","temperature_2m":9.2,"weather_code":45,"is_day":0},)"
                R"("daily":{"time":["2026-10-14"],"temperature_2m_max":[19.2],"temperature_2m_min":[9.0]}})"};
    }
    Net::Http::Response post_json(const std::string&,
                                  const std::string&,
                                  const std::vector<std::pair<std::string, std::string>>&,
                                  long) override {
        throw std::runtime_error("FakeOpenMeteo: POST is not expected");
    }
};

class NowSettingsApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::TodayController c;
    FakeOpenMeteo meteo;

    std::string config_file_name() const override { return "now_settings_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["now"]["enabled"] = true; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Net::Http::install_for_testing(&meteo);
        if (Cache::is_initialized()) {
            Cache::get().del(Now::Weather::cache_key(43.25249, 76.9115));
            Cache::get().del(Now::Weather::down_key(43.25249, 76.9115));
        }
        Database::get().execute_write([](auto& txn) {
            for (const char* id : {kAnna, kBoris}) {
                const std::string o = id;
                txn.exec_params("DELETE FROM now_settings WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM day_note WHERE owner_id = $1::uuid", o);
                txn.exec_params(
                    "INSERT INTO users (id, email, confirmed, role_id) "
                    "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                    "ON CONFLICT DO NOTHING",
                    o,
                    o + "@example.test");
            }
            return true;
        });
    }

    void TearDown() override {
        Net::Http::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    using Handler = void (Api::TodayController::*)(const HttpRequestPtr&,
                                                   std::function<void(const HttpResponsePtr&)>&&);

    HttpResponsePtr call(Handler h,
                         HttpMethod m,
                         const json& body = json::object(),
                         const std::vector<std::pair<std::string, std::string>>& params = {},
                         const std::string& who = kAnna) {
        auto req = TestHelpers::authed_json(user(who), body, m);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        (c.*h)(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    HttpResponsePtr put_settings(const json& body) { return call(&Api::TodayController::putSettings, Put, body); }
};

}  // namespace

TEST_F(NowSettingsApiTest, DefaultsWithoutARow) {
    auto r = call(&Api::TodayController::getSettings, Get);
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    const auto d = body_of(r)["data"];
    EXPECT_TRUE(d["city"].is_null());
    EXPECT_EQ(d["corners"], json({{"tl", "body"}, {"tr", "goals"}, {"bl", "day"}, {"br", "tasks"}}));
    auto w = call(&Api::TodayController::weather, Get);
    ASSERT_EQ(w->statusCode(), k200OK);
    EXPECT_TRUE(body_of(w)["data"].is_null()) << "no city, no weather";
    EXPECT_TRUE(meteo.urls.empty());
}

TEST_F(NowSettingsApiTest, ALayoutIsCheckedAndKept) {
    EXPECT_EQ(
        put_settings({{"corners", {{"tl", "tasks"}, {"tr", "tasks"}, {"bl", nullptr}, {"br", nullptr}}}})->statusCode(),
        k400BadRequest)
        << "a chip twice";
    auto ok = put_settings({{"corners", {{"tl", "tasks"}, {"tr", nullptr}, {"bl", "day"}, {"br", "body"}}}});
    ASSERT_EQ(ok->statusCode(), k200OK) << std::string(ok->body());
    EXPECT_TRUE(body_of(ok)["data"]["corners"]["tr"].is_null());
    auto other = call(&Api::TodayController::getSettings, Get, json::object(), {}, kBoris);
    EXPECT_EQ(body_of(other)["data"]["corners"]["tl"], "body") << "Boris keeps the default";
}

TEST_F(NowSettingsApiTest, TheCityIsGeocoded) {
    auto r = put_settings({{"city", "Almaty"}});
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    const auto d = body_of(r)["data"];
    EXPECT_EQ(d["city"], "Almaty");
    EXPECT_EQ(d["country_code"], "KZ");
    EXPECT_DOUBLE_EQ(d["latitude"].get<double>(), 43.25249);
    EXPECT_EQ(put_settings({{"city", "Nowhereville"}})->statusCode(), k422UnprocessableEntity);
    EXPECT_EQ(body_of(call(&Api::TodayController::getSettings, Get))["data"]["city"], "Almaty")
        << "an unknown city changes nothing";
    meteo.down = true;
    EXPECT_EQ(put_settings({{"city", "Almaty"}})->statusCode(), k503ServiceUnavailable);
    meteo.down = false;
    auto cleared = put_settings({{"city", nullptr}});
    ASSERT_EQ(cleared->statusCode(), k200OK);
    EXPECT_TRUE(body_of(cleared)["data"]["latitude"].is_null());
}

TEST_F(NowSettingsApiTest, TheWeatherIsFetchedThenCached) {
    ASSERT_EQ(put_settings({{"city", "Almaty"}})->statusCode(), k200OK);
    meteo.urls.clear();
    auto first = call(&Api::TodayController::weather, Get);
    ASSERT_EQ(first->statusCode(), k200OK) << std::string(first->body());
    const auto w = body_of(first)["data"];
    EXPECT_EQ(w["place"], "Almaty");
    EXPECT_DOUBLE_EQ(w["temperature"].get<double>(), 9.2);
    EXPECT_EQ(w["words"], "fog");
    ASSERT_EQ(meteo.urls.size(), 1U);
    EXPECT_NE(meteo.urls[0].find("latitude=43.2525"), std::string::npos);
    if (Cache::is_initialized()) {
        meteo.down = true;
        auto second = call(&Api::TodayController::weather, Get);
        ASSERT_EQ(second->statusCode(), k200OK);
        EXPECT_EQ(body_of(second)["cached"], true);
        EXPECT_EQ(meteo.urls.size(), 1U) << "the second answer comes from the cache";
    }
}

TEST_F(NowSettingsApiTest, TheWeatherDownIsA503AndIsNotAskedAgainAtOnce) {
    ASSERT_EQ(put_settings({{"city", "Almaty"}})->statusCode(), k200OK);
    meteo.urls.clear();
    meteo.down = true;
    EXPECT_EQ(call(&Api::TodayController::weather, Get)->statusCode(), k503ServiceUnavailable);
    ASSERT_EQ(meteo.urls.size(), 1U);
    if (Cache::is_initialized()) {
        EXPECT_EQ(call(&Api::TodayController::weather, Get)->statusCode(), k503ServiceUnavailable);
        EXPECT_EQ(meteo.urls.size(), 1U) << "a failure is remembered for a few minutes: no second slow call";
    }
}

TEST_F(NowSettingsApiTest, TheNoteOfADayIsStoredButNotOnToday) {
    EXPECT_EQ(call(&Api::TodayController::putNote, Put, {{"date", kDay}, {"text", ""}})->statusCode(), k400BadRequest);
    ASSERT_EQ(call(&Api::TodayController::putNote, Put, {{"date", kDay}, {"text", "Pack"}})->statusCode(), k200OK);
    ASSERT_EQ(call(&Api::TodayController::putNote, Put, {{"date", kDay}, {"text", "Pack the adapter"}})->statusCode(),
              k200OK)
        << "a second save replaces";
    EXPECT_EQ(Repositories::Now::NoteRepository().get(kAnna, kDay), std::optional<std::string>("Pack the adapter"));
    EXPECT_FALSE(Repositories::Now::NoteRepository().get(kBoris, kDay)) << "the note is the owner's only";
    auto t = call(&Api::TodayController::today, Get, json::object(), {{"date", kDay}, {"tz", "Asia/Almaty"}});
    ASSERT_EQ(t->statusCode(), k200OK) << std::string(t->body());
    EXPECT_FALSE(body_of(t)["data"].contains("note")) << "the Now screen no longer reads the note";
    EXPECT_EQ(call(&Api::TodayController::deleteNote, Delete, json::object(), {{"date", kDay}})->statusCode(),
              k204NoContent);
    EXPECT_FALSE(Repositories::Now::NoteRepository().get(kAnna, kDay));
}
