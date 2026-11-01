/**
 * @file test_now_day_api.cpp
 * @brief The day ribbon routes (api/NowDayController.cpp): a focus label is
 *        cut at 200 characters, never inside a character, so a mixed or
 *        Cyrillic label is kept and valid; fields of the wrong JSON type and
 *        a ?tz= that names no zone are answered 400, not 500 and not a silent
 *        UTC.
 */

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/NowDayController.hpp"
#include "database/Database.hpp"
#include "test_helpers.hpp"
#include "utils/Utf8.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaad0b1";

using TestHelpers::body_of;
using TestHelpers::user;

/// RFC 3339 in UTC, @p ago seconds before now.
std::string iso_ago(long long ago) {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) - ago;
    std::tm tm{};
    gmtime_r(&t, &tm);
    char out[32];
    std::strftime(out, sizeof out, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return out;
}

class NowDayApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::NowDayController c;

    std::string config_file_name() const override { return "now_day_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["now"]["enabled"] = true; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            const std::string o = kAnna;
            txn.exec_params("DELETE FROM focus_sessions WHERE owner_id = $1::uuid", o);
            txn.exec_params(
                "INSERT INTO users (id, email, confirmed, role_id) "
                "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                "ON CONFLICT DO NOTHING",
                o,
                o + "@example.test");
            return true;
        });
    }

    using Handler = void (Api::NowDayController::*)(const HttpRequestPtr&,
                                                    std::function<void(const HttpResponsePtr&)>&&);

    static const char* spec_path_of(Handler h) {
        if (h == &Api::NowDayController::day)
            return "/api/v1/now/day";
        if (h == &Api::NowDayController::addFocus)
            return "/api/v1/now/focus-sessions";
        if (h == &Api::NowDayController::focusStats)
            return "/api/v1/now/focus-stats";
        return nullptr;
    }

    HttpResponsePtr call(Handler h,
                         HttpMethod m,
                         const json& body = json::object(),
                         const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto req = TestHelpers::authed_json(user(kAnna), body, m);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        (c.*h)(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        if (const char* path = spec_path_of(h))
            TestHelpers::expect_2xx_matches_schema(captured, m, path);
        return captured;
    }

    HttpResponsePtr add(const json& body) { return call(&Api::NowDayController::addFocus, Post, body); }

    std::string stored_label() {
        return Database::get().execute_read([](auto& txn) {
            auto r = txn.exec_params("SELECT label FROM focus_sessions WHERE owner_id = $1::uuid", std::string(kAnna));
            return r.empty() ? std::string("<none>") : r[0][0].template as<std::string>();
        });
    }
};

}  // namespace

// 151 ASCII letters and 100 Cyrillic ones: the old 200-byte cut ended on the first
// byte of a letter, PostgreSQL refused the text (22021) and the session was lost.
TEST_F(NowDayApiTest, AMixedLabelIsCutByCharacters) {
    std::string label(151, 'a');
    for (int i = 0; i < 100; ++i) {
        label += "\xD0\xB6";  // "ж"
    }
    auto r = add({{"started_at", iso_ago(1800)}, {"ended_at", iso_ago(60)}, {"label", label}});
    ASSERT_EQ(r->statusCode(), k201Created) << std::string(r->body());
    const std::string kept = stored_label();
    EXPECT_EQ(Utils::Utf8::length(kept), 200u);
    EXPECT_EQ(kept, Utils::Utf8::cut(label, 200));
}

TEST_F(NowDayApiTest, ACyrillicLabelKeepsItsTwoHundredLetters) {
    std::string label;
    for (int i = 0; i < 200; ++i) {
        label += "\xD0\xB6";
    }
    auto r = add({{"started_at", iso_ago(1800)}, {"ended_at", iso_ago(60)}, {"label", label}});
    ASSERT_EQ(r->statusCode(), k201Created) << std::string(r->body());
    EXPECT_EQ(stored_label(), label) << "a byte cut kept only half of it";
}

TEST_F(NowDayApiTest, WrongTypesAre400) {
    EXPECT_EQ(add({{"started_at", 1}, {"ended_at", iso_ago(60)}})->statusCode(), k400BadRequest);
    EXPECT_EQ(add({{"started_at", iso_ago(1800)}, {"ended_at", nullptr}})->statusCode(), k400BadRequest);
    auto r = add({{"started_at", iso_ago(1800)}, {"ended_at", iso_ago(60)}, {"label", 42}});
    ASSERT_EQ(r->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(r)["error"], "invalid_label");
    EXPECT_EQ(stored_label(), "<none>");
    auto ok = add({{"started_at", iso_ago(1800)}, {"ended_at", iso_ago(60)}, {"label", nullptr}});
    ASSERT_EQ(ok->statusCode(), k201Created) << "a null label is no label";
    EXPECT_EQ(stored_label(), "");
}

TEST_F(NowDayApiTest, AZoneThatIsNoneIs400) {
    auto day = call(&Api::NowDayController::day, Get, json::object(), {{"date", "2026-10-29"}, {"tz", "Mars/Olympus"}});
    EXPECT_EQ(day->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(day)["error"], "invalid_query");
    auto stats = call(&Api::NowDayController::focusStats, Get, json::object(), {{"tz", "Asia/Almty"}});
    EXPECT_EQ(stats->statusCode(), k400BadRequest);
    EXPECT_EQ(call(&Api::NowDayController::focusStats, Get, json::object(), {{"tz", "Asia/Almaty"}})->statusCode(),
              k200OK);
    EXPECT_EQ(call(&Api::NowDayController::focusStats, Get)->statusCode(), k200OK) << "no tz is UTC";
    EXPECT_EQ(
        call(&Api::NowDayController::day, Get, json::object(), {{"date", "2026-10-29"}, {"tz", "UTC"}})->statusCode(),
        k200OK);
}
