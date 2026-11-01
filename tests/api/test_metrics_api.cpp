/**
 * @file test_metrics_api.cpp
 * @brief /api/v1/metrics: the catalog expanded for the caller (their
 *        currencies, categories and habits ticked by hand), an entry the caller
 *        cannot read carrying the reason, one series by local day, the checks
 *        of its query, a source naming another user's category answering with
 *        no rows, and 404 while goals and habits are both off.
 */

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/MetricsController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "habits/Habits.hpp"
#include "repositories/habits/Days.hpp"
#include "repositories/metrics/Series.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaab52";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbb52";
constexpr const char* kAnnaMi = "metrics-api-anna-mi";
constexpr const char* kAnnaFood = "52525252-0000-4000-8000-0000000000a1";
constexpr const char* kBorisFood = "52525252-0000-4000-8000-0000000000b1";
constexpr const char* kWater = "52525252-0000-4000-8000-0000000000a2";

Security::Auth::AuthPrincipal user(const char* who, std::uint32_t permissions) {
    Security::Auth::AuthPrincipal p;
    p.subject = who;
    p.raw_claims = json{{"sub", who}, {"permissions", permissions}};
    return p;
}

Security::Auth::AuthPrincipal reader(const char* who) {
    return user(who, Domain::Permission::kGeneral | Domain::Permission::kFitnessRead);
}

using TestHelpers::body_of;

void sql(const std::string& statement) {
    Database::get().execute_write([&](auto& txn) {
        txn.exec(statement);
        return true;
    });
}

class MetricsApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::MetricsController c;

    std::string config_file_name() const override { return "metrics_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["habits"]["enabled"] = true;
        cfg["goals"]["enabled"] = true;
        cfg["fitness"]["enabled"] = true;
        cfg["money"]["enabled"] = true;
        cfg["practice"]["enabled"] = true;
        cfg["workout"]["enabled"] = true;
        cfg["tasks"]["enabled"] = true;
        cfg["food"]["enabled"] = false;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            for (const char* id : {kAnna, kBoris}) {
                const std::string o = id;
                txn.exec_params(
                    "INSERT INTO users (id, email, confirmed, role_id) "
                    "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                    "ON CONFLICT DO NOTHING",
                    o,
                    o + "@example.test");
                for (const char* table : {"money_transactions",
                                          "money_transfers",
                                          "money_accounts",
                                          "money_categories",
                                          "money_currencies",
                                          "practice_entries",
                                          "habits",
                                          "mi_accounts"}) {
                    txn.exec_params(std::string("DELETE FROM ") + table + " WHERE owner_id = $1::uuid", o);
                }
            }
            txn.exec_params("DELETE FROM body_measurements WHERE user_id = $1", std::string(kAnnaMi));
            return true;
        });
        for (const char* o : {kAnna, kBoris}) {
            sql(std::string("INSERT INTO money_currencies (owner_id, code) VALUES ('") + o + "', 'KZT')");
        }
        sql(std::string("INSERT INTO money_accounts (id, owner_id, name, currency) VALUES "
                        "('52525252-0000-4000-8000-0000000000a0', '") +
            kAnna + "', 'Card', 'KZT'), ('52525252-0000-4000-8000-0000000000b0', '" + kBoris + "', 'Card', 'KZT')");
        sql(std::string("INSERT INTO money_categories (id, owner_id, name, kind) VALUES ('") + kAnnaFood + "', '" +
            kAnna + "', 'Food', 'expense'), ('" + kBorisFood + "', '" + kBoris + "', 'Food', 'expense')");
        sql(std::string("INSERT INTO money_transactions (owner_id, type, date, account_id, amount, category_id, name) "
                        "VALUES ('") +
            kAnna + "', 'expense', '2026-10-01', '52525252-0000-4000-8000-0000000000a0', 1500, '" + kAnnaFood +
            "', 'Lunch'), ('" + kBoris + "', 'expense', '2026-10-01', '52525252-0000-4000-8000-0000000000b0', 3000, '" +
            kBorisFood + "', 'Boris')");
        sql(std::string("INSERT INTO practice_entries (owner_id, kind, date, amount, currency) VALUES ('") + kAnna +
            "', 'income', '2026-10-01', 1000, 'USD')");
        sql(std::string("INSERT INTO habits (id, owner_id, name, schedule, source, unit) VALUES ('") + kWater + "', '" +
            kAnna + "', 'Water', 'daily', 'manual', 'l'), (gen_random_uuid(), '" + kAnna +
            "', 'Train', 'daily', 'workout', '')");
    }

    using Fn = std::function<void(const HttpRequestPtr&, Api::MetricsController::Callback&&)>;

    HttpResponsePtr call(const Security::Auth::AuthPrincipal& who,
                         const Fn& fn,
                         const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto req = TestHelpers::authed_json(who, json::object(), Get);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        fn(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    json catalog(const Security::Auth::AuthPrincipal& who) {
        auto r = call(who, [&](auto req, auto cb) { c.list(req, std::move(cb)); });
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        TestHelpers::expect_2xx_matches_schema(r, "GET", "/api/v1/metrics");
        return body_of(r)["data"];
    }

    HttpResponsePtr series(const Security::Auth::AuthPrincipal& who,
                           const std::string& source,
                           const std::string& from,
                           const std::string& to,
                           const std::string& tz = "UTC") {
        auto r = call(who,
                      [&](auto req, auto cb) { c.series(req, std::move(cb)); },
                      {{"source", source}, {"from", from}, {"to", to}, {"tz", tz}});
        TestHelpers::expect_2xx_matches_schema(r, "GET", "/api/v1/metrics/series");
        return r;
    }

    static json entry_of(const json& list, const std::string& id) {
        for (const auto& e : list) {
            if (e["id"] == id) {
                return e;
            }
        }
        ADD_FAILURE() << "no entry " << id;
        return json::object();
    }

    static bool has(const json& list, const std::string& id) {
        for (const auto& e : list) {
            if (e["id"] == id) {
                return true;
            }
        }
        return false;
    }
};

class MetricsOffTest : public MetricsApiTest {
protected:
    std::string config_file_name() const override { return "metrics_off_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        MetricsApiTest::config_overrides(cfg);
        cfg["habits"]["enabled"] = false;
        cfg["goals"]["enabled"] = false;
    }
};

}  // namespace

TEST_F(MetricsApiTest, CatalogListsExpandedEntries) {
    const json list = catalog(reader(kAnna));
    const json steps = entry_of(list, "fitness:steps");
    EXPECT_EQ(steps["module"], "fitness");
    EXPECT_EQ(steps["unit"], "steps");
    EXPECT_EQ(steps["reduce"], "sum");
    EXPECT_EQ(steps["presence"], false);
    EXPECT_EQ(entry_of(list, "fitness:weight")["presence"], true);

    const json kzt = entry_of(list, "money:expense:KZT");
    EXPECT_EQ(kzt["unit"], "KZT");
    EXPECT_EQ(kzt["available"], true);
    EXPECT_TRUE(kzt["why"].is_null());
    EXPECT_TRUE(has(list, "money:income:KZT"));
    EXPECT_TRUE(has(list, "money:balance:KZT"));
    EXPECT_TRUE(has(list, std::string("money:expense:KZT:") + kAnnaFood));
    EXPECT_FALSE(has(list, std::string("money:expense:KZT:") + kBorisFood)) << "Boris's category";
    EXPECT_FALSE(has(list, "money:expense:USD")) << "no USD account";
    EXPECT_TRUE(has(list, "practice:income:USD")) << "a practice entry in USD";
    EXPECT_TRUE(has(list, "practice:income:KZT")) << "the currency of a money account";

    const json water = entry_of(list, std::string("habits:") + kWater);
    EXPECT_EQ(water["label"], "Water");
    EXPECT_EQ(water["unit"], "l");
    int habits = 0;
    for (const auto& e : list) {
        const std::string id = e["id"].get<std::string>();
        EXPECT_EQ(id.find('{'), std::string::npos) << "a template left in the list: " << id;
        habits += id.rfind("habits:", 0) == 0 && id != "habits:mood" ? 1 : 0;
    }
    EXPECT_EQ(habits, 1) << "only the habit ticked by hand";

    const json boris = catalog(reader(kBoris));
    EXPECT_FALSE(has(boris, std::string("habits:") + kWater));
    EXPECT_TRUE(has(boris, std::string("money:expense:KZT:") + kBorisFood));
}

TEST_F(MetricsApiTest, UnavailableEntryCarriesWhy) {
    const json list = catalog(reader(kAnna));
    const json steps = entry_of(list, "fitness:steps");
    EXPECT_EQ(steps["available"], false);
    EXPECT_EQ(steps["why"], "not_linked");
    const json kcal = entry_of(list, "food:kcal");
    EXPECT_EQ(kcal["available"], false);
    EXPECT_EQ(kcal["why"], "module_off");
    EXPECT_EQ(entry_of(list, "tape:waist")["available"], true) << "the tape needs no Mi account";

    const json plain = catalog(user(kAnna, Domain::Permission::kGeneral));
    EXPECT_EQ(entry_of(plain, "fitness:steps")["why"], "no_permission");

    sql(std::string("INSERT INTO mi_accounts (owner_id, xiaomi_user_id, pass_token_sealed, nonce, region) VALUES ('") +
        kAnna + "', '" + kAnnaMi + "', 'x', 'x', 'sg')");
    EXPECT_EQ(entry_of(catalog(reader(kAnna)), "fitness:steps")["available"], true);
}

TEST_F(MetricsApiTest, SeriesAnswersDays) {
    auto r = series(reader(kAnna), "money:expense:KZT", "2026-09-30", "2026-10-02");
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    json body = body_of(r);
    EXPECT_EQ(body["source"], "money:expense:KZT");
    EXPECT_EQ(body["unit"], "KZT");
    EXPECT_TRUE(body["why"].is_null());
    ASSERT_EQ(body["data"].size(), 1U);
    EXPECT_EQ(body["data"][0]["date"], "2026-10-01");
    EXPECT_DOUBLE_EQ(body["data"][0]["value"].get<double>(), 1500);

    // A weigh-in at 19:30 UTC is 00:30 of the next day in Almaty.
    sql(std::string("INSERT INTO mi_accounts (owner_id, xiaomi_user_id, pass_token_sealed, nonce, region) VALUES ('") +
        kAnna + "', '" + kAnnaMi + "', 'x', 'x', 'sg')");
    sql(std::string("INSERT INTO body_measurements (user_id, timestamp, weight_kg) VALUES ('") + kAnnaMi +
        "', '2026-10-01 19:30Z', 80.2)");
    r = series(reader(kAnna), "weight", "2026-10-01", "2026-10-02", "Asia/Almaty");
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    body = body_of(r);
    EXPECT_EQ(body["source"], "fitness:weight") << "a legacy name answers under its catalog id";
    EXPECT_EQ(body["unit"], "kg");
    ASSERT_EQ(body["data"].size(), 1U);
    EXPECT_EQ(body["data"][0]["date"], "2026-10-02");
    EXPECT_DOUBLE_EQ(body["data"][0]["value"].get<double>(), 80.2);

    sql(std::string("INSERT INTO habit_checks (owner_id, habit_id, date, value) VALUES ('") + kAnna + "', '" + kWater +
        "', '2026-10-01', NULL)");
    r = series(reader(kAnna), std::string("habits:") + kWater, "2026-10-01", "2026-10-01");
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    body = body_of(r);
    EXPECT_EQ(body["unit"], "l") << "the habit's own unit";
    ASSERT_EQ(body["data"].size(), 1U);
    EXPECT_TRUE(body["data"][0]["value"].is_null()) << "a tick without a value";
}

TEST_F(MetricsApiTest, SeriesRefusesBadRange) {
    const auto refused =
        [&](const std::string& source, const std::string& from, const std::string& to, const std::string& tz = "UTC") {
            auto r = series(reader(kAnna), source, from, to, tz);
            EXPECT_EQ(r->statusCode(), k400BadRequest) << source << " " << from << ".." << to << " " << tz;
            EXPECT_EQ(body_of(r)["error"], "invalid_query") << std::string(r->body());
        };
    refused("unknown:id", "2026-10-01", "2026-10-02");
    refused("money:expense:kzt", "2026-10-01", "2026-10-02");
    refused("habits:x", "2026-10-01", "2026-10-02");
    refused("", "2026-10-01", "2026-10-02");
    refused("fitness:steps", "2026-10-02", "2026-10-01");
    refused("fitness:steps", "2026-13-01", "2026-10-02");
    refused("fitness:steps", "", "2026-10-02");
    refused("fitness:steps", "2025-01-01", "2026-02-05");  // 401 days
    refused("fitness:steps", "2026-10-01", "2026-10-02", "Mars/Olympus");
    // 400 days, the most: 2025-09-01 .. 2026-10-05.
    auto r = series(reader(kAnna), "tape:waist", "2025-09-01", "2026-10-05");
    EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
}

TEST_F(MetricsApiTest, ForeignSourceIsNotYours) {
    auto r = series(reader(kAnna), std::string("money:expense:KZT:") + kBorisFood, "2026-10-01", "2026-10-01");
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    json body = body_of(r);
    EXPECT_EQ(body["why"], "not_yours");
    EXPECT_TRUE(body["data"].empty()) << "Boris's 3000 never reaches Anna";

    r = series(reader(kBoris), std::string("habits:") + kWater, "2026-10-01", "2026-10-01");
    ASSERT_EQ(r->statusCode(), k200OK);
    EXPECT_EQ(body_of(r)["why"], "not_yours");
    r = series(reader(kAnna), "money:expense:USD", "2026-10-01", "2026-10-01");
    ASSERT_EQ(r->statusCode(), k200OK);
    EXPECT_EQ(body_of(r)["why"], "not_yours");
    r = series(reader(kAnna), "food:kcal", "2026-10-01", "2026-10-01");
    ASSERT_EQ(r->statusCode(), k200OK);
    EXPECT_EQ(body_of(r)["why"], "module_off");
}

// The bank recounts the 1500 lunch by +40 a day later: the adjustment row has no category of
// its own, and counts in the category of the expense it adjusts, as the money report does.
TEST_F(MetricsApiTest, ACategorySeriesCountsTheBanksAdjustment) {
    sql(std::string("INSERT INTO money_transactions (owner_id, type, date, account_id, amount, adjusts_id, name) "
                    "SELECT owner_id, 'fx_adjustment', '2026-10-02', account_id, 40, id, 'Recount' FROM "
                    "money_transactions WHERE owner_id = '") +
        kAnna + "' AND name = 'Lunch'");
    auto r = series(reader(kAnna), std::string("money:expense:KZT:") + kAnnaFood, "2026-10-01", "2026-10-02");
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    json body = body_of(r);
    ASSERT_EQ(body["data"].size(), 2U) << body.dump();
    EXPECT_DOUBLE_EQ(body["data"][0]["value"].get<double>(), 1500);
    EXPECT_EQ(body["data"][1]["date"], "2026-10-02");
    EXPECT_DOUBLE_EQ(body["data"][1]["value"].get<double>(), 40);
    r = series(reader(kAnna), "money:expense:KZT", "2026-10-02", "2026-10-02");
    ASSERT_EQ(body_of(r)["data"].size(), 1U);
    EXPECT_DOUBLE_EQ(body_of(r)["data"][0]["value"].get<double>(), 40) << "the whole currency agrees";
}

// "Spend at most 5000 KZT a day", made on 2026-09-28: the days without spending are done
// (value 0), the 1500 lunch is done, the day before the habit is not, today is open.
TEST_F(MetricsApiTest, AnAtMostSpendingHabitIsDoneOnADayWithoutSpending) {
    sql(std::string("INSERT INTO habits (owner_id, name, schedule, source, goal, direction, created_at) VALUES ('") +
        kAnna + "', 'Spend less', 'daily', 'money:expense:KZT', 5000, 'at_most', '2026-09-28T10:00:00Z')");
    const auto access = Repositories::Metrics::access_of(kAnna, Domain::Permission::kGeneral);
    const auto day = [](const char* t) { return ::Habits::day_of(t); };
    const auto all = Repositories::Habits::evaluate(
        kAnna, day("2026-09-27"), day("2026-10-03"), day("2026-10-03"), "UTC", access, true);
    const Repositories::Habits::Evaluated* spend = nullptr;
    for (const auto& e : all) {
        if (e.habit["name"] == "Spend less") {
            spend = &e;
        }
    }
    ASSERT_NE(spend, nullptr);
    EXPECT_FALSE(spend->no_data);
    std::map<std::string, ::Habits::DayState> by_day;
    for (const auto& [d, s] : spend->days) {
        by_day[::Habits::text_of(d)] = s;
    }
    EXPECT_FALSE(by_day["2026-09-27"].done) << "before the habit there was nothing to keep";
    EXPECT_TRUE(by_day["2026-09-28"].done);
    EXPECT_TRUE(by_day["2026-09-30"].done) << "no spending at all is the best day";
    ASSERT_TRUE(by_day["2026-09-30"].value.has_value());
    EXPECT_DOUBLE_EQ(*by_day["2026-09-30"].value, 0);
    EXPECT_TRUE(by_day["2026-10-01"].done) << "1500 <= 5000";
    EXPECT_TRUE(by_day["2026-10-02"].done);
    EXPECT_FALSE(by_day["2026-10-03"].done) << "today is still open";
    EXPECT_EQ(spend->streak, 5) << "2026-09-28 .. 2026-10-02";
}

TEST_F(MetricsOffTest, ModulesOffAnswer404) {
    auto r = call(reader(kAnna), [&](auto req, auto cb) { c.list(req, std::move(cb)); });
    EXPECT_EQ(r->statusCode(), k404NotFound);
    r = series(reader(kAnna), "fitness:steps", "2026-10-01", "2026-10-01");
    EXPECT_EQ(r->statusCode(), k404NotFound);
}
