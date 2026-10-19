/**
 * @file test_practice_api.cpp
 * @brief The practice module under /api/v1/practice: clients, deals with
 *        stages, incomes and expenses in their own currencies, the months
 *        with totals per currency (two currencies never added together), the
 *        owner scope, the 404 while the module is off, and a task linked to
 *        a deal as its next step.
 */

#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/PracticeController.hpp"
#include "api/TasksController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaa0531";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbb0532";

Security::Auth::AuthPrincipal principal(const char* who) {
    Security::Auth::AuthPrincipal p;
    p.subject = who;
    p.raw_claims = json{{"sub", who}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

using TestHelpers::body_of;
using Params = std::vector<std::pair<std::string, std::string>>;

void clean_and_seed_users() {
    Database::get().execute_write([](auto& txn) {
        for (const char* id : {kAnna, kBoris}) {
            const std::string o = id;
            txn.exec_params("DELETE FROM task_items WHERE owner_id = $1::uuid", o);
            txn.exec_params("DELETE FROM practice_entries WHERE owner_id = $1::uuid", o);
            txn.exec_params("DELETE FROM practice_deals WHERE owner_id = $1::uuid", o);
            txn.exec_params("DELETE FROM practice_clients WHERE owner_id = $1::uuid", o);
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

class PracticeApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::PracticeController c;
    Api::TasksController tasks;

    std::string config_file_name() const override { return "practice_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["practice"]["enabled"] = true;
        cfg["tasks"]["enabled"] = true;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        clean_and_seed_users();
    }

    template <typename Fn>
    HttpResponsePtr call(const char* who, HttpMethod method, const json& body, Fn fn, const Params& params = {}) {
        auto req = TestHelpers::authed_json(principal(who), body, method);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        fn(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    HttpResponsePtr add_client(const char* who, const json& body) {
        auto r = call(who, Post, body, [&](auto req, auto cb) { c.createClient(req, std::move(cb)); });
        TestHelpers::expect_2xx_matches_schema(r, "POST", "/api/v1/practice/clients");
        return r;
    }

    std::string client(const char* who, const char* name = "Acme") {
        auto r = add_client(who, json{{"name", name}, {"contact", "anna@acme.test"}});
        EXPECT_EQ(r->statusCode(), k201Created) << std::string(r->body());
        return body_of(r)["data"]["id"].get<std::string>();
    }

    HttpResponsePtr add_deal(const char* who, const json& body) {
        auto r = call(who, Post, body, [&](auto req, auto cb) { c.createDeal(req, std::move(cb)); });
        TestHelpers::expect_2xx_matches_schema(r, "POST", "/api/v1/practice/deals");
        return r;
    }

    std::string deal(const char* who, const std::string& client_id, const char* stage = "active") {
        auto r = add_deal(who,
                          json{{"client_id", client_id},
                               {"title", "Landing page"},
                               {"stage", stage},
                               {"value", 1500},
                               {"currency", "USD"}});
        EXPECT_EQ(r->statusCode(), k201Created) << std::string(r->body());
        return body_of(r)["data"]["id"].get<std::string>();
    }

    HttpResponsePtr add_entry(const char* who, const json& body) {
        auto r = call(who, Post, body, [&](auto req, auto cb) { c.createEntry(req, std::move(cb)); });
        TestHelpers::expect_2xx_matches_schema(r, "POST", "/api/v1/practice/entries");
        return r;
    }

    json entries(const char* who, const Params& params = {}) {
        auto r = call(
            who, Get, json::object(), [&](auto req, auto cb) { c.listEntries(req, std::move(cb)); }, params);
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        TestHelpers::expect_2xx_matches_schema(r, "GET", "/api/v1/practice/entries");
        return body_of(r);
    }

    json deal_row(const char* who, const std::string& id) {
        auto r = call(who, Get, json::object(), [&](auto req, auto cb) { c.getDeal(req, std::move(cb), id); });
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        TestHelpers::expect_2xx_matches_schema(r, "GET", "/api/v1/practice/deals/{id}");
        return body_of(r)["data"];
    }
};

/// A total of @p currency in a totals list, or null.
json total_of(const json& totals, const std::string& currency) {
    for (const auto& t : totals) {
        if (t["currency"] == currency) {
            return t;
        }
    }
    return nullptr;
}

}  // namespace

TEST_F(PracticeApiTest, Clients) {
    auto r = add_client(kAnna, json{{"name", "Acme"}, {"contact", "+7 701 000 00 00"}, {"note", "Found on Upwork"}});
    ASSERT_EQ(r->statusCode(), k201Created) << std::string(r->body());
    const auto row = body_of(r)["data"];
    EXPECT_EQ(row["name"], "Acme");
    EXPECT_EQ(row["open_deals"], 0);
    EXPECT_EQ(row["income"], json::array());
    const std::string id = row["id"];

    EXPECT_EQ(add_client(kAnna, json{{"name", ""}})->statusCode(), k400BadRequest);
    EXPECT_EQ(add_client(kAnna, json{{"contact", "no name"}})->statusCode(), k400BadRequest);

    auto edited = call(kAnna, Patch, json{{"note", "Pays on time"}}, [&](auto req, auto cb) {
        c.updateClient(req, std::move(cb), id);
    });
    ASSERT_EQ(edited->statusCode(), k200OK) << std::string(edited->body());
    TestHelpers::expect_matches_schema(edited, "PATCH", "/api/v1/practice/clients/{id}");
    EXPECT_EQ(body_of(edited)["data"]["note"], "Pays on time");
    EXPECT_EQ(body_of(edited)["data"]["name"], "Acme") << "untouched fields stay";

    client(kAnna, "Beta");
    auto list = call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.listClients(req, std::move(cb)); });
    TestHelpers::expect_2xx_matches_schema(list, "GET", "/api/v1/practice/clients");
    ASSERT_EQ(body_of(list)["data"].size(), 2U);
    EXPECT_EQ(body_of(list)["data"][0]["name"], "Acme") << "by name";

    auto theirs = call(kBoris, Get, json::object(), [&](auto req, auto cb) { c.getClient(req, std::move(cb), id); });
    EXPECT_EQ(theirs->statusCode(), k404NotFound) << "owner scope";
    auto none = call(kBoris, Get, json::object(), [&](auto req, auto cb) { c.listClients(req, std::move(cb)); });
    EXPECT_EQ(body_of(none)["data"].size(), 0U);
}

TEST_F(PracticeApiTest, DealsStagesAndValues) {
    const auto acme = client(kAnna);
    const auto theirs = client(kBoris);
    EXPECT_EQ(add_deal(kAnna, json{{"title", "No client"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(add_deal(kAnna, json{{"client_id", theirs}, {"title", "x"}})->statusCode(), k400BadRequest)
        << "a client of someone else";
    EXPECT_EQ(add_deal(kAnna, json{{"client_id", acme}, {"title", "x"}, {"stage", "won"}})->statusCode(),
              k400BadRequest);
    EXPECT_EQ(add_deal(kAnna, json{{"client_id", acme}, {"title", "x"}, {"value", 100}})->statusCode(), k400BadRequest)
        << "a value without its currency";
    EXPECT_EQ(add_deal(kAnna, json{{"client_id", acme}, {"title", "x"}, {"value", 1500.5}, {"currency", "JPY"}})
                  ->statusCode(),
              k400BadRequest)
        << "yen has no decimals";
    EXPECT_EQ(
        add_deal(kAnna,
                 json{{"client_id", acme}, {"title", "x"}, {"start_date", "2026-10-10"}, {"end_date", "2026-10-01"}})
            ->statusCode(),
        k400BadRequest);

    auto lead = add_deal(kAnna, json{{"client_id", acme}, {"title", "Audit"}});
    ASSERT_EQ(lead->statusCode(), k201Created) << std::string(lead->body());
    EXPECT_EQ(body_of(lead)["data"]["stage"], "lead") << "a new deal is a lead";
    EXPECT_TRUE(body_of(lead)["data"]["value"].is_null());
    const auto done = deal(kAnna, acme, "done");
    const auto active = deal(kAnna, acme, "active");

    auto list = call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.listDeals(req, std::move(cb)); });
    TestHelpers::expect_2xx_matches_schema(list, "GET", "/api/v1/practice/deals");
    const auto rows = body_of(list)["data"];
    ASSERT_EQ(rows.size(), 3U);
    EXPECT_EQ(rows[0]["stage"], "lead");
    EXPECT_EQ(rows[1]["stage"], "active");
    EXPECT_EQ(rows[2]["stage"], "done") << "open stages first";
    EXPECT_EQ(rows[1]["client_name"], "Acme");
    EXPECT_EQ(rows[1]["value"], 1500);
    EXPECT_EQ(rows[1]["currency"], "USD");

    auto only = call(
        kAnna, Get, json::object(), [&](auto req, auto cb) { c.listDeals(req, std::move(cb)); }, {{"stage", "done"}});
    EXPECT_EQ(body_of(only)["data"].size(), 1U);
    auto bad = call(
        kAnna, Get, json::object(), [&](auto req, auto cb) { c.listDeals(req, std::move(cb)); }, {{"stage", "won"}});
    EXPECT_EQ(bad->statusCode(), k400BadRequest);

    auto moved =
        call(kAnna, Patch, json{{"stage", "done"}, {"value", nullptr}, {"currency", nullptr}}, [&](auto req, auto cb) {
            c.updateDeal(req, std::move(cb), active);
        });
    ASSERT_EQ(moved->statusCode(), k200OK) << std::string(moved->body());
    TestHelpers::expect_matches_schema(moved, "PATCH", "/api/v1/practice/deals/{id}");
    EXPECT_EQ(body_of(moved)["data"]["stage"], "done");
    EXPECT_TRUE(body_of(moved)["data"]["currency"].is_null());

    auto clients = call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.listClients(req, std::move(cb)); });
    EXPECT_EQ(body_of(clients)["data"][0]["open_deals"], 1) << "the lead only";

    auto in_use =
        call(kAnna, Delete, json::object(), [&](auto req, auto cb) { c.deleteClient(req, std::move(cb), acme); });
    EXPECT_EQ(in_use->statusCode(), k409Conflict) << "a client with deals stays";
    EXPECT_EQ(call(kBoris, Delete, json::object(), [&](auto req, auto cb) { c.deleteDeal(req, std::move(cb), done); })
                  ->statusCode(),
              k404NotFound);
}

TEST_F(PracticeApiTest, EntriesBelongToTheDealsClient) {
    const auto acme = client(kAnna);
    const auto beta = client(kAnna, "Beta");
    const auto site = deal(kAnna, acme);

    auto paid = add_entry(kAnna,
                          json{{"kind", "income"},
                               {"date", "2026-10-03"},
                               {"amount", 1000},
                               {"currency", "USD"},
                               {"deal_id", site},
                               {"note", "First half"}});
    ASSERT_EQ(paid->statusCode(), k201Created) << std::string(paid->body());
    const auto row = body_of(paid)["data"];
    EXPECT_EQ(row["client_id"], acme) << "the deal's client";
    EXPECT_EQ(row["client_name"], "Acme");
    EXPECT_EQ(row["deal_title"], "Landing page");
    EXPECT_EQ(row["amount"], 1000);

    EXPECT_EQ(add_entry(kAnna,
                        json{{"kind", "income"},
                             {"date", "2026-10-03"},
                             {"amount", 10},
                             {"currency", "USD"},
                             {"deal_id", site},
                             {"client_id", beta}})
                  ->statusCode(),
              k400BadRequest)
        << "another client than the deal's";
    EXPECT_EQ(add_entry(kAnna, json{{"kind", "gift"}, {"date", "2026-10-03"}, {"amount", 10}, {"currency", "USD"}})
                  ->statusCode(),
              k400BadRequest);
    EXPECT_EQ(add_entry(kAnna, json{{"kind", "income"}, {"date", "2026-10-03"}, {"amount", 0}, {"currency", "USD"}})
                  ->statusCode(),
              k400BadRequest)
        << "positive";
    EXPECT_EQ(
        add_entry(kAnna, json{{"kind", "income"}, {"date", "2026-10-03"}, {"amount", 10.001}, {"currency", "USD"}})
            ->statusCode(),
        k400BadRequest)
        << "a cent is the smallest unit of USD";
    const auto theirs = deal(kBoris, client(kBoris));
    EXPECT_EQ(
        add_entry(
            kAnna,
            json{{"kind", "income"}, {"date", "2026-10-03"}, {"amount", 10}, {"currency", "USD"}, {"deal_id", theirs}})
            ->statusCode(),
        k400BadRequest)
        << "a deal of someone else";

    add_entry(kAnna, json{{"kind", "expense"}, {"date", "2026-09-20"}, {"amount", 20}, {"currency", "USD"}});
    EXPECT_EQ(entries(kAnna)["total"], 2);
    EXPECT_EQ(entries(kAnna, {{"kind", "expense"}})["data"].size(), 1U);
    EXPECT_EQ(entries(kAnna, {{"month", "2026-10"}})["data"].size(), 1U);
    EXPECT_EQ(entries(kAnna, {{"deal", site}})["data"].size(), 1U);
    EXPECT_EQ(entries(kBoris)["total"], 0);
    auto bad = call(kAnna,
                    Get,
                    json::object(),
                    [&](auto req, auto cb) { c.listEntries(req, std::move(cb)); },
                    {{"month", "2026-13"}});
    EXPECT_EQ(bad->statusCode(), k400BadRequest);

    // The deal moves to Beta: its entries move with it.
    auto moved = call(
        kAnna, Patch, json{{"client_id", beta}}, [&](auto req, auto cb) { c.updateDeal(req, std::move(cb), site); });
    ASSERT_EQ(moved->statusCode(), k200OK) << std::string(moved->body());
    const std::string id = row["id"];
    auto now = call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.getEntry(req, std::move(cb), id); });
    TestHelpers::expect_2xx_matches_schema(now, "GET", "/api/v1/practice/entries/{id}");
    EXPECT_EQ(body_of(now)["data"]["client_id"], beta);
    EXPECT_EQ(deal_row(kAnna, site)["received"], json::array({json{{"currency", "USD"}, {"amount", 1000}}}));

    auto edited = call(kAnna, Patch, json{{"amount", 1200}, {"deal_id", nullptr}}, [&](auto req, auto cb) {
        c.updateEntry(req, std::move(cb), id);
    });
    ASSERT_EQ(edited->statusCode(), k200OK) << std::string(edited->body());
    TestHelpers::expect_matches_schema(edited, "PATCH", "/api/v1/practice/entries/{id}");
    EXPECT_EQ(body_of(edited)["data"]["amount"], 1200);
    EXPECT_TRUE(body_of(edited)["data"]["deal_id"].is_null());
    EXPECT_EQ(body_of(edited)["data"]["client_id"], beta) << "the client stays";
    auto yen = call(kAnna, Patch, json{{"currency", "JPY"}, {"amount", 0.5}}, [&](auto req, auto cb) {
        c.updateEntry(req, std::move(cb), id);
    });
    EXPECT_EQ(yen->statusCode(), k400BadRequest);
    EXPECT_EQ(call(kAnna, Delete, json::object(), [&](auto req, auto cb) { c.deleteEntry(req, std::move(cb), id); })
                  ->statusCode(),
              k204NoContent);
}

TEST_F(PracticeApiTest, MonthsTotalPerCurrencyNeverAcross) {
    const auto acme = client(kAnna);
    const auto beta = client(kAnna, "Beta");
    const auto add =
        [&](const char* kind, const char* date, double amount, const char* currency, const std::string& who) {
            json body{{"kind", kind}, {"date", date}, {"amount", amount}, {"currency", currency}};
            if (!who.empty()) {
                body["client_id"] = who;
            }
            auto r = add_entry(kAnna, body);
            ASSERT_EQ(r->statusCode(), k201Created) << std::string(r->body());
        };
    add("income", "2026-10-03", 1000, "USD", acme);
    add("income", "2026-10-05", 250000, "KZT", beta);
    add("expense", "2026-10-06", 20, "USD", "");
    add("expense", "2026-09-06", 12.5, "EUR", "");
    add("income", "2025-12-31", 5, "USD", acme);

    auto months = call(
        kAnna, Get, json::object(), [&](auto req, auto cb) { c.listMonths(req, std::move(cb)); }, {{"year", "2026"}});
    ASSERT_EQ(months->statusCode(), k200OK) << std::string(months->body());
    TestHelpers::expect_matches_schema(months, "GET", "/api/v1/practice/months");
    const auto m = body_of(months);
    EXPECT_EQ(m["year"], 2026);
    ASSERT_EQ(m["data"].size(), 2U) << "October and September; 2025 left out";
    EXPECT_EQ(m["data"][0]["month"], "2026-10");
    EXPECT_EQ(m["data"][0]["entries"], 3);
    const auto oct = m["data"][0]["totals"];
    ASSERT_EQ(oct.size(), 2U) << "one total per currency";
    EXPECT_EQ(total_of(oct, "USD")["income"], 1000);
    EXPECT_EQ(total_of(oct, "USD")["expense"], 20);
    EXPECT_EQ(total_of(oct, "USD")["net"], 980);
    EXPECT_EQ(total_of(oct, "KZT")["income"], 250000);
    EXPECT_EQ(total_of(oct, "KZT")["net"], 250000) << "KZT is not added to USD";
    EXPECT_EQ(total_of(m["totals"], "EUR")["net"], -12.5);
    EXPECT_EQ(m["totals"].size(), 3U);

    auto one = call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.getMonth(req, std::move(cb), "2026-10"); });
    ASSERT_EQ(one->statusCode(), k200OK) << std::string(one->body());
    TestHelpers::expect_matches_schema(one, "GET", "/api/v1/practice/months/{month}");
    const auto d = body_of(one)["data"];
    EXPECT_EQ(d["month"], "2026-10");
    EXPECT_EQ(d["entries"].size(), 3U);
    EXPECT_EQ(d["totals"].size(), 2U);
    ASSERT_EQ(d["by_client"].size(), 3U) << "Acme USD, Beta KZT, no client USD";
    EXPECT_EQ(d["by_client"][0]["client_name"], "Acme");
    EXPECT_TRUE(d["by_client"][2]["client_id"].is_null());

    EXPECT_EQ(call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.getMonth(req, std::move(cb), "2026-1"); })
                  ->statusCode(),
              k400BadRequest);
    auto bad_year = call(
        kAnna, Get, json::object(), [&](auto req, auto cb) { c.listMonths(req, std::move(cb)); }, {{"year", "26"}});
    EXPECT_EQ(bad_year->statusCode(), k400BadRequest);
    auto none = call(
        kBoris, Get, json::object(), [&](auto req, auto cb) { c.listMonths(req, std::move(cb)); }, {{"year", "2026"}});
    EXPECT_EQ(body_of(none)["data"].size(), 0U) << "owner scope";
}

TEST_F(PracticeApiTest, ATaskIsANextStepOfADeal) {
    const auto site = deal(kAnna, client(kAnna));
    const auto theirs = deal(kBoris, client(kBoris));
    const auto add_task = [&](const char* who, const json& body) {
        return call(who, Post, body, [&](auto req, auto cb) { tasks.createItem(req, std::move(cb)); });
    };
    auto t = add_task(kAnna, json{{"title", "Send the estimate"}, {"area", "projects"}, {"deal_id", site}});
    ASSERT_EQ(t->statusCode(), k201Created) << std::string(t->body());
    TestHelpers::expect_matches_schema(t, "POST", "/api/v1/tasks/items");
    EXPECT_EQ(body_of(t)["data"]["deal_id"], site);
    EXPECT_EQ(body_of(t)["data"]["deal_title"], "Landing page");
    EXPECT_EQ(add_task(kAnna, json{{"title", "x"}, {"area", "projects"}, {"deal_id", theirs}})->statusCode(),
              k400BadRequest)
        << "a deal of someone else";
    add_task(kAnna, json{{"title", "Unrelated"}, {"area", "projects"}});

    EXPECT_EQ(deal_row(kAnna, site)["open_tasks"], 1);
    auto steps = call(
        kAnna, Get, json::object(), [&](auto req, auto cb) { tasks.listItems(req, std::move(cb)); }, {{"deal", site}});
    ASSERT_EQ(steps->statusCode(), k200OK) << std::string(steps->body());
    EXPECT_EQ(body_of(steps)["data"].size(), 1U);

    const std::string task_id = body_of(t)["data"]["id"];
    EXPECT_EQ(call(kAnna, Delete, json::object(), [&](auto req, auto cb) { c.deleteDeal(req, std::move(cb), site); })
                  ->statusCode(),
              k204NoContent);
    auto left =
        call(kAnna, Get, json::object(), [&](auto req, auto cb) { tasks.getItem(req, std::move(cb), task_id); });
    ASSERT_EQ(left->statusCode(), k200OK);
    EXPECT_TRUE(body_of(left)["data"]["deal_id"].is_null()) << "the task stays, unlinked";
}

namespace {

class PracticeOffTest : public TestHelpers::CoreBackedTest {
protected:
    Api::PracticeController c;
    std::string config_file_name() const override { return "practice_off_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["practice"]["enabled"] = false; }
};

}  // namespace

TEST_F(PracticeOffTest, TheModuleIsAbsentWhileOff) {
    auto req = TestHelpers::authed_json(principal(kAnna), json::object(), Get);
    HttpResponsePtr r;
    c.listClients(req, [&](const HttpResponsePtr& resp) { r = resp; });
    ASSERT_TRUE(r);
    EXPECT_EQ(r->statusCode(), k404NotFound);
    HttpResponsePtr m;
    c.getMonth(
        req, [&](const HttpResponsePtr& resp) { m = resp; }, "2026-10");
    ASSERT_TRUE(m);
    EXPECT_EQ(m->statusCode(), k404NotFound);
}
