/**
 * @file test_travel_api.cpp
 * @brief /api/v1/travel: the module switch, the owner scope on every kind of
 *        id, the two rule templates, counters over seeded crossings, a trip's
 *        spends in one currency (partial without a rate), a deleted trip
 *        leaving its tasks and spends, and tasks taking a trip.
 */

#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/TasksController.hpp"
#include "api/TravelController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaac11";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbc12";

Security::Auth::AuthPrincipal principal(const char* who) {
    Security::Auth::AuthPrincipal p;
    p.subject = who;
    p.raw_claims = json{{"sub", who}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

json body_of(const HttpResponsePtr& resp) {
    return json::parse(std::string(resp->body()));
}

class TravelApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::TravelController c;
    Api::TasksController tasks;

    std::string config_file_name() const override { return "travel_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["travel"]["enabled"] = true;
        cfg["tasks"]["enabled"] = true;
        cfg["money"]["enabled"] = true;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE travel_trip_items, travel_trips, travel_rules, travel_crossings, travel_settings");
            for (const char* id : {kAnna, kBoris}) {
                const std::string o = id;
                txn.exec_params("DELETE FROM task_items WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM money_transactions WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM money_accounts WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM money_categories WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM money_currencies WHERE owner_id = $1::uuid", o);
            }
            return true;
        });
    }

    HttpResponsePtr call(const char* who,
                         HttpMethod method,
                         const json& body,
                         const std::function<void(const HttpRequestPtr&, Api::TravelController::Callback&&)>& fn,
                         const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto req = TestHelpers::authed_json(principal(who), body, method);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        fn(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    json created_trip(const char* who) {
        auto r = call(who,
                      Post,
                      json{{"name", "Thailand"}, {"start_date", "2026-11-01"}, {"end_date", "2026-11-20"}},
                      [&](auto req, auto cb) { c.createTrip(req, std::move(cb)); });
        EXPECT_EQ(r->statusCode(), k201Created) << std::string(r->body());
        return body_of(r)["data"];
    }

    static std::string spend(const char* owner, const char* date, const char* currency, double amount) {
        return Database::get().execute_write([&](auto& txn) {
            const std::string o = owner;
            txn.exec_params(
                "INSERT INTO money_currencies (owner_id, code) VALUES ($1::uuid, $2) ON CONFLICT DO NOTHING",
                o,
                std::string(currency));
            auto r = txn.exec_params(
                "WITH a AS (INSERT INTO money_accounts (owner_id, name, currency) VALUES ($1::uuid, 'Card', $3) "
                " RETURNING id), cat AS (INSERT INTO money_categories (owner_id, name) VALUES ($1::uuid, 'Food') "
                " RETURNING id) "
                "INSERT INTO money_transactions (owner_id, type, date, account_id, category_id, amount, name) "
                "SELECT $1::uuid, 'expense', $2::date, a.id, cat.id, $4, 'Lunch' FROM a, cat RETURNING id::text",
                o,
                std::string(date),
                std::string(currency),
                amount);
            return r[0][0].template as<std::string>();
        });
    }
};

}  // namespace

TEST_F(TravelApiTest, ACrossingIsRecordedAndChecked) {
    auto ok = call(kAnna,
                   Post,
                   json{{"date", "2026-08-03"}, {"from", "KZ"}, {"to", "GE"}, {"time", "23:40"}},
                   [&](auto req, auto cb) { c.createCrossing(req, std::move(cb)); });
    ASSERT_EQ(ok->statusCode(), k201Created) << std::string(ok->body());
    EXPECT_EQ(body_of(ok)["data"]["to_country"], "GE");
    for (const json& bad : {json{{"date", "2026-08-03"}, {"from", "kz"}, {"to", "GE"}},
                            json{{"date", "2026-02-30"}, {"from", "KZ"}, {"to", "GE"}},
                            json{{"date", "2026-08-03"}, {"from", "KZ"}, {"to", "KZ"}},
                            json{{"date", "2026-08-03"}, {"from", "KZ"}, {"to", "GE"}, {"time", "25:00"}}}) {
        auto r = call(kAnna, Post, bad, [&](auto req, auto cb) { c.createCrossing(req, std::move(cb)); });
        EXPECT_EQ(r->statusCode(), k400BadRequest) << bad.dump();
    }
}

TEST_F(TravelApiTest, AnotherOwnersIdsAreNotFound) {
    const auto trip = created_trip(kAnna)["id"].get<std::string>();
    auto crossing = body_of(call(kAnna,
                                 Post,
                                 json{{"date", "2026-08-03"}, {"from", "KZ"}, {"to", "GE"}},
                                 [&](auto req, auto cb) { c.createCrossing(req, std::move(cb)); }))["data"]["id"]
                        .get<std::string>();
    EXPECT_EQ(call(kBoris, Get, json::object(), [&](auto req, auto cb) { c.getTrip(req, std::move(cb), trip); })
                  ->statusCode(),
              k404NotFound);
    EXPECT_EQ(call(kBoris, Delete, json::object(), [&](auto req, auto cb) { c.deleteTrip(req, std::move(cb), trip); })
                  ->statusCode(),
              k404NotFound);
    EXPECT_EQ(
        call(kBoris, Delete, json::object(), [&](auto req, auto cb) { c.deleteCrossing(req, std::move(cb), crossing); })
            ->statusCode(),
        k404NotFound);
    EXPECT_EQ(call(kBoris,
                   Post,
                   json{{"kind", "note"}, {"date", "2026-11-02"}, {"title", "x"}},
                   [&](auto req, auto cb) { c.addItem(req, std::move(cb), trip); })
                  ->statusCode(),
              k404NotFound);
    EXPECT_EQ(call(kBoris, Get, json::object(), [&](auto req, auto cb) { c.candidateSpends(req, std::move(cb), trip); })
                  ->statusCode(),
              k404NotFound);
    auto list = call(kBoris, Get, json::object(), [&](auto req, auto cb) { c.listTrips(req, std::move(cb)); });
    EXPECT_EQ(body_of(list)["data"].size(), 0U);
}

TEST_F(TravelApiTest, TemplatesAndCounters) {
    ASSERT_EQ(
        call(kAnna, Post, json::object(), [&](auto req, auto cb) { c.addTemplate(req, std::move(cb), "schengen"); })
            ->statusCode(),
        k201Created);
    EXPECT_EQ(call(kAnna, Post, json::object(), [&](auto req, auto cb) { c.addTemplate(req, std::move(cb), "nope"); })
                  ->statusCode(),
              k404NotFound);
    for (const json& x : {json{{"date", "2026-01-01"}, {"from", "KZ"}, {"to", "FR"}},
                          json{{"date", "2026-01-30"}, {"from", "FR"}, {"to", "KZ"}}}) {
        call(kAnna, Post, x, [&](auto req, auto cb) { c.createCrossing(req, std::move(cb)); });
    }
    auto r = call(kAnna,
                  Get,
                  json::object(),
                  [&](auto req, auto cb) { c.counters(req, std::move(cb)); },
                  {{"date", "2026-02-15"}});
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    const auto counters = body_of(r)["data"];
    ASSERT_EQ(counters.size(), 1U);
    EXPECT_EQ(counters[0]["used"], 30) << "Jan 1 – Jan 30 in France";
    EXPECT_EQ(counters[0]["left"], 60);
    EXPECT_EQ(counters[0]["reentry"], "2026-05-01");
    EXPECT_NE(counters[0]["source"].get<std::string>().find("2016/399"), std::string::npos);
    EXPECT_EQ(body_of(r)["current_country"], json::array({"KZ"}));
}

TEST_F(TravelApiTest, ARuleIsChecked) {
    auto bad = call(kAnna,
                    Post,
                    json{{"name", "x"},
                         {"countries", {"fr"}},
                         {"window_kind", "rolling_days"},
                         {"window_days", 180},
                         {"threshold_days", 90},
                         {"threshold_kind", "at_most"}},
                    [&](auto req, auto cb) { c.createRule(req, std::move(cb)); });
    EXPECT_EQ(bad->statusCode(), k400BadRequest);
    auto no_window = call(kAnna,
                          Post,
                          json{{"name", "x"},
                               {"countries", {"FR"}},
                               {"window_kind", "rolling_days"},
                               {"threshold_days", 90},
                               {"threshold_kind", "at_most"}},
                          [&](auto req, auto cb) { c.createRule(req, std::move(cb)); });
    EXPECT_EQ(no_window->statusCode(), k400BadRequest) << "a rolling window needs its days";
    auto ok = call(kAnna,
                   Post,
                   json{{"name", "Thailand visa-free"},
                        {"countries", {"TH"}},
                        {"window_kind", "since_last_entry"},
                        {"threshold_days", 60},
                        {"threshold_kind", "at_most"}},
                   [&](auto req, auto cb) { c.createRule(req, std::move(cb)); });
    EXPECT_EQ(ok->statusCode(), k201Created) << std::string(ok->body());
}

TEST_F(TravelApiTest, SpendsInOneCurrencyArePartialWithoutARate) {
    const auto trip = created_trip(kAnna)["id"].get<std::string>();
    const auto usd = spend(kAnna, "2026-11-03", "USD", 40);
    const auto xyz = spend(kAnna, "2026-11-04", "XYZ", 1000);
    auto candidates =
        call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.candidateSpends(req, std::move(cb), trip); });
    EXPECT_EQ(body_of(candidates)["data"].size(), 2U);
    auto linked = call(
        kAnna, Post, json{{"link", {usd, xyz}}}, [&](auto req, auto cb) { c.linkSpends(req, std::move(cb), trip); });
    EXPECT_EQ(body_of(linked)["data"]["changed"], 2);
    auto r = call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.getTrip(req, std::move(cb), trip); });
    const auto spends = body_of(r)["data"]["spends"];
    EXPECT_EQ(spends["by_currency"].size(), 2U);
    EXPECT_DOUBLE_EQ(spends["total"]["amount"].get<double>(), 40.0) << "USD counts, XYZ has no rate";
    EXPECT_TRUE(spends["total"]["partial"].get<bool>());
}

TEST_F(TravelApiTest, ADeletedTripLeavesItsTasksAndSpends) {
    const auto trip = created_trip(kAnna)["id"].get<std::string>();
    auto task =
        call(kAnna, Post, json{{"title", "Adapter"}, {"area", "travel"}, {"trip_id", trip}}, [&](auto req, auto cb) {
            tasks.createItem(req, std::move(cb));
        });
    ASSERT_EQ(task->statusCode(), k201Created) << std::string(task->body());
    EXPECT_EQ(body_of(task)["data"]["trip_id"], trip);
    const auto s = spend(kAnna, "2026-11-03", "USD", 10);
    call(kAnna, Post, json{{"link", {s}}}, [&](auto req, auto cb) { c.linkSpends(req, std::move(cb), trip); });
    auto view = call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.getTrip(req, std::move(cb), trip); });
    EXPECT_EQ(body_of(view)["data"]["packing"]["total"], 1);
    EXPECT_EQ(call(kAnna, Delete, json::object(), [&](auto req, auto cb) { c.deleteTrip(req, std::move(cb), trip); })
                  ->statusCode(),
              k204NoContent);
    const auto left = Database::get().execute_read([&](auto& txn) {
        auto r = txn.exec_params(
            "SELECT (SELECT count(*) FROM task_items WHERE owner_id = $1::uuid AND trip_id IS NULL), "
            " (SELECT count(*) FROM money_transactions WHERE owner_id = $1::uuid AND trip_id IS NULL)",
            std::string(kAnna));
        return std::pair<long, long>{r[0][0].template as<long>(), r[0][1].template as<long>()};
    });
    EXPECT_EQ(left, (std::pair<long, long>{1, 1}));
}

TEST_F(TravelApiTest, ATaskTakesOnlyTheCallersTrip) {
    const auto trip = created_trip(kAnna)["id"].get<std::string>();
    auto theirs =
        call(kBoris, Post, json{{"title", "Adapter"}, {"area", "travel"}, {"trip_id", trip}}, [&](auto req, auto cb) {
            tasks.createItem(req, std::move(cb));
        });
    EXPECT_EQ(theirs->statusCode(), k400BadRequest);
    call(kAnna, Post, json{{"title", "Adapter"}, {"area", "travel"}, {"trip_id", trip}}, [&](auto req, auto cb) {
        tasks.createItem(req, std::move(cb));
    });
    call(kAnna, Post, json{{"title", "Other"}, {"area", "travel"}}, [&](auto req, auto cb) {
        tasks.createItem(req, std::move(cb));
    });
    auto req = TestHelpers::authed_json(principal(kAnna), json::object(), Get);
    req->setParameter("trip", trip);
    HttpResponsePtr r;
    tasks.listItems(req, [&](const HttpResponsePtr& x) { r = x; });
    ASSERT_TRUE(r);
    EXPECT_EQ(body_of(r)["data"].size(), 1U) << "the trip's packing only";
}
