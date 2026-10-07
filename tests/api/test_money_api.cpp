/**
 * @file test_money_api.cpp
 * @brief Money module routes: two users apart, the invariants as 400s,
 *        balances through transfers, reports per currency with the "as if"
 *        block, rates, settings, the module switch.
 */

#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/MoneyController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/money/FxRateRepository.hpp"
#include "repositories/money/ParseJobRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa7";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb7";
constexpr const char* kMissingId = "55555555-5555-4555-8555-555555555555";

Security::Auth::AuthPrincipal user(const std::string& id) {
    Security::Auth::AuthPrincipal p;
    p.subject = id;
    p.raw_claims = json{{"sub", id}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

json body_of(const HttpResponsePtr& resp) {
    return json::parse(std::string(resp->body()));
}

class MoneyApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::MoneyController controller;

    std::string config_file_name() const override { return "money_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["money"]["enabled"] = true; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec(
                "TRUNCATE TABLE money_settings, money_fx_rates, money_merchants, money_transfers, money_transactions, "
                "money_categories, money_accounts, money_currencies");
            for (const char* id : {kAnna, kBoris}) {
                txn.exec_params(
                    "INSERT INTO users (id, email, confirmed, role_id) "
                    "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                    "ON CONFLICT DO NOTHING",
                    std::string(id),
                    std::string(id) + "@example.test");
            }
            return true;
        });
        // The first list seeds the owner's ten currencies.
        call(&Api::MoneyController::listCurrencies, user(kAnna), Get);
        call(&Api::MoneyController::listCurrencies, user(kBoris), Get);
    }

    using Handler = void (Api::MoneyController::*)(const HttpRequestPtr&, Api::MoneyController::Callback&&);
    using IdHandler = void (Api::MoneyController::*)(const HttpRequestPtr&,
                                                     Api::MoneyController::Callback&&,
                                                     const std::string&);

    HttpResponsePtr call(Handler h,
                         const Security::Auth::AuthPrincipal& p,
                         HttpMethod m,
                         const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto req = TestHelpers::authed(p, m);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        (controller.*h)(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    HttpResponsePtr call_json(Handler h,
                              const Security::Auth::AuthPrincipal& p,
                              const json& body,
                              HttpMethod m = Post) {
        HttpResponsePtr captured;
        (controller.*h)(TestHelpers::authed_json(p, body, m), [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    HttpResponsePtr call_id(IdHandler h,
                            const Security::Auth::AuthPrincipal& p,
                            HttpMethod m,
                            const std::string& id,
                            const json& body = json::object()) {
        HttpResponsePtr captured;
        (controller.*h)(
            TestHelpers::authed_json(p, body, m), [&](const HttpResponsePtr& r) { captured = r; }, id);
        EXPECT_TRUE(captured);
        return captured;
    }

    json created(Handler h, const std::string& who, const json& body) {
        const auto resp = call_json(h, user(who), body);
        EXPECT_EQ(resp->statusCode(), k201Created) << resp->body();
        return body_of(resp)["data"];
    }

    json account(const std::string& who, const std::string& name, const std::string& currency, double opening = 0) {
        return created(&Api::MoneyController::createAccount,
                       who,
                       json{{"name", name}, {"currency", currency}, {"opening_balance", opening}});
    }

    json category(const std::string& who, const json& body) {
        return created(&Api::MoneyController::createCategory, who, body);
    }

    json expense(
        const json& acc, const json& cat, double amount, const std::string& date, const std::string& merchant) {
        return created(&Api::MoneyController::createTransaction,
                       kAnna,
                       json{{"date", date},
                            {"account_id", acc["id"]},
                            {"amount", amount},
                            {"category_id", cat["id"]},
                            {"merchant", merchant},
                            {"name", merchant}});
    }

    double balance_of(const json& acc) {
        const auto resp = call_id(&Api::MoneyController::getAccount, user(kAnna), Get, acc["id"]);
        return body_of(resp)["data"]["balance"].get<double>();
    }
};

}  // namespace

TEST_F(MoneyApiTest, ReferenceDataIsPerUserAndValidated) {
    const json list = body_of(call(&Api::MoneyController::listCurrencies, user(kAnna), Get))["data"];
    EXPECT_EQ(list.size(), 10u);
    EXPECT_EQ(call_json(&Api::MoneyController::upsertCurrency, user(kAnna), json{{"code", "kzt"}})->statusCode(),
              k400BadRequest);
    EXPECT_EQ(call_json(&Api::MoneyController::upsertCurrency, user(kAnna), json{{"code", "JPY"}, {"decimals", 0}})
                  ->statusCode(),
              k200OK);

    const json kaspi = account(kAnna, "Kaspi Gold Visa •8880", "KZT", 1000);
    EXPECT_EQ(kaspi["balance"], 1000);
    EXPECT_EQ(call_json(&Api::MoneyController::createAccount, user(kAnna), json{{"name", "x"}, {"currency", "XXX"}})
                  ->statusCode(),
              k400BadRequest)
        << "a currency the user does not keep";
    EXPECT_EQ(call_json(&Api::MoneyController::createAccount,
                        user(kAnna),
                        json{{"name", "x"}, {"currency", "KZT"}, {"last4", "12345"}})
                  ->statusCode(),
              k400BadRequest);
    EXPECT_EQ(call_id(&Api::MoneyController::getAccount, user(kBoris), Get, kaspi["id"])->statusCode(), k404NotFound);
    EXPECT_EQ(call_id(&Api::MoneyController::updateAccount, user(kAnna), Patch, kaspi["id"], json{{"currency", "USD"}})
                  ->statusCode(),
              k400BadRequest)
        << "an account keeps its currency";

    EXPECT_EQ(call_json(&Api::MoneyController::createCategory, user(kAnna), json{{"name", "Food"}, {"budget_max", 10}})
                  ->statusCode(),
              k400BadRequest)
        << "a budget needs its currency";
    const json food = category(kAnna, json{{"name", "Food"}, {"budget_max", 1000}, {"budget_currency", "KZT"}});
    EXPECT_EQ(food["budget_currency"], "KZT");
    EXPECT_EQ(body_of(call(&Api::MoneyController::listCategories, user(kBoris), Get))["data"].size(), 0u);
}

TEST_F(MoneyApiTest, TransactionsKeepTheInvariants) {
    const json kaspi = account(kAnna, "Kaspi", "KZT", 1000);
    const json freedom = account(kAnna, "Freedom", "KZT");
    const json boris = account(kBoris, "Boris", "KZT");
    const json food = category(kAnna, json{{"name", "Food"}});
    const json salary = category(kAnna, json{{"name", "Salary"}, {"kind", "income"}});

    const json t1 = expense(kaspi, food, 13275.61, "2026-10-05", "Big C Phuket");
    EXPECT_EQ(t1["currency"], "KZT");
    EXPECT_EQ(t1["merchant_key"], "big c phuket");

    const auto post = [&](const json& b) {
        return call_json(&Api::MoneyController::createTransaction, user(kAnna), b)->statusCode();
    };
    const json base{{"date", "2026-10-05"},
                    {"account_id", kaspi["id"]},
                    {"amount", 10},
                    {"category_id", food["id"]},
                    {"name", "x"}};
    auto with = [&](const char* k, const json& v) {
        json b = base;
        b[k] = v;
        return b;
    };
    EXPECT_EQ(post(with("amount", -5)), k400BadRequest);
    EXPECT_EQ(post(with("category_id", salary["id"])), k400BadRequest) << "an expense in an income category";
    EXPECT_EQ(post(with("account_id", boris["id"])), k400BadRequest) << "another user's account";
    EXPECT_EQ(post(with("date", "2026-02-30")), k400BadRequest);
    EXPECT_EQ(post(with("receipt_amount", 955.75)), k400BadRequest) << "the receipt's amount without its currency";
    EXPECT_EQ(post(with("adjusts_id", t1["id"])), k400BadRequest) << "only an adjustment points at a row";
    json no_category = base;
    no_category.erase("category_id");
    EXPECT_EQ(post(no_category), k400BadRequest);

    const json adj{{"type", "fx_adjustment"},
                   {"date", "2026-10-07"},
                   {"account_id", kaspi["id"]},
                   {"amount", -20},
                   {"adjusts_id", t1["id"]},
                   {"name", "Bank recalculation"}};
    EXPECT_EQ(post(adj), k201Created) << "a negative adjustment: the bank returned money";
    json other = adj;
    other["account_id"] = freedom["id"];
    EXPECT_EQ(post(other), k400BadRequest) << "an adjustment sits on its original's account";

    const json row = body_of(call_id(&Api::MoneyController::getTransaction, user(kAnna), Get, t1["id"]))["data"];
    EXPECT_DOUBLE_EQ(row["final_amount"].get<double>(), 13255.61);
    EXPECT_EQ(row["adjustments"].size(), 1u);
    EXPECT_EQ(call_id(&Api::MoneyController::getTransaction, user(kBoris), Get, t1["id"])->statusCode(), k404NotFound);
    EXPECT_EQ(call_id(&Api::MoneyController::updateTransaction, user(kAnna), Patch, t1["id"], json{{"type", "income"}})
                  ->statusCode(),
              k400BadRequest);

    // Deleting the original takes its adjustment along.
    EXPECT_EQ(call_id(&Api::MoneyController::deleteTransaction, user(kAnna), Delete, t1["id"])->statusCode(),
              k204NoContent);
    EXPECT_EQ(body_of(call(&Api::MoneyController::listTransactions, user(kAnna), Get))["total"], 0);
}

TEST_F(MoneyApiTest, BatchIsAllOrNothingAndTheListFilters) {
    const json kaspi = account(kAnna, "Kaspi", "KZT");
    const json cash = account(kAnna, "Cash THB", "THB");
    const json food = category(kAnna, json{{"name", "Food"}});
    const json good{{"date", "2026-10-05"},
                    {"account_id", kaspi["id"]},
                    {"amount", 10},
                    {"category_id", food["id"]},
                    {"name", "a"}};
    json bad = good;
    bad["amount"] = 0;
    EXPECT_EQ(call_json(&Api::MoneyController::createTransactions, user(kAnna), json{{"transactions", {good, bad}}})
                  ->statusCode(),
              k400BadRequest);
    json thb = good;
    thb["account_id"] = cash["id"];
    const auto ok =
        call_json(&Api::MoneyController::createTransactions, user(kAnna), json{{"transactions", {good, thb}}});
    ASSERT_EQ(ok->statusCode(), k201Created) << ok->body();
    EXPECT_EQ(body_of(ok)["count"], 2);
    EXPECT_EQ(body_of(call(&Api::MoneyController::listTransactions, user(kAnna), Get, {{"currency", "THB"}}))["total"],
              1);
    EXPECT_EQ(call(&Api::MoneyController::listTransactions, user(kAnna), Get, {{"from", "2026-13-01"}})->statusCode(),
              k400BadRequest);
    EXPECT_EQ(body_of(call(&Api::MoneyController::listTransactions, user(kBoris), Get))["total"], 0);
}

TEST_F(MoneyApiTest, TransfersAndBalances) {
    const json kaspi = account(kAnna, "Kaspi", "KZT", 100000);
    const json freedom = account(kAnna, "Freedom", "KZT");
    const json cash = account(kAnna, "Cash THB", "THB");
    const auto post = [&](const json& b) { return call_json(&Api::MoneyController::createTransfer, user(kAnna), b); };
    EXPECT_EQ(post(json{{"date", "2026-10-06"},
                        {"from_account_id", kaspi["id"]},
                        {"to_account_id", freedom["id"]},
                        {"amount_sent", 1000},
                        {"amount_received", 999}})
                  ->statusCode(),
              k400BadRequest)
        << "same currency: no amount received";
    EXPECT_EQ(post(json{{"date", "2026-10-06"},
                        {"from_account_id", kaspi["id"]},
                        {"to_account_id", cash["id"]},
                        {"amount_sent", 13000}})
                  ->statusCode(),
              k400BadRequest)
        << "across currencies the amount received is needed";
    EXPECT_EQ(post(json{{"date", "2026-10-06"},
                        {"from_account_id", kaspi["id"]},
                        {"to_account_id", kaspi["id"]},
                        {"amount_sent", 1}})
                  ->statusCode(),
              k400BadRequest);
    ASSERT_EQ(post(json{{"date", "2026-10-06"},
                        {"from_account_id", kaspi["id"]},
                        {"to_account_id", freedom["id"]},
                        {"amount_sent", 1000},
                        {"fee", 10}})
                  ->statusCode(),
              k201Created);
    const auto x = post(json{{"date", "2026-10-06"},
                             {"from_account_id", kaspi["id"]},
                             {"to_account_id", cash["id"]},
                             {"amount_sent", 13000},
                             {"amount_received", 1000}});
    ASSERT_EQ(x->statusCode(), k201Created) << x->body();
    EXPECT_EQ(body_of(x)["data"]["cost_rate"], 13);

    EXPECT_DOUBLE_EQ(balance_of(kaspi), 100000 - 1010 - 13000);
    EXPECT_DOUBLE_EQ(balance_of(freedom), 1000);
    EXPECT_DOUBLE_EQ(balance_of(cash), 1000);
    const json groups = body_of(call(&Api::MoneyController::balances, user(kAnna), Get))["data"];
    ASSERT_EQ(groups.size(), 2u) << "one group per currency, never one total";
    EXPECT_EQ(groups[0]["currency"], "KZT");
    EXPECT_DOUBLE_EQ(groups[0]["total"].get<double>(), 100000 - 1010 - 13000 + 1000);

    EXPECT_EQ(call_id(&Api::MoneyController::updateTransfer,
                      user(kAnna),
                      Patch,
                      body_of(x)["data"]["id"],
                      json{{"to_account_id", freedom["id"]}})
                  ->statusCode(),
              k400BadRequest);
    EXPECT_EQ(
        call_id(&Api::MoneyController::deleteTransfer, user(kBoris), Delete, body_of(x)["data"]["id"])->statusCode(),
        k404NotFound);
}

TEST_F(MoneyApiTest, InboxAndMerchantMemory) {
    const json kaspi = account(kAnna, "Kaspi", "KZT");
    const json food = category(kAnna, json{{"name", "Food"}});
    const json boris_food = category(kBoris, json{{"name", "Food"}});
    expense(kaspi, food, 500, "2026-10-05", "Big C");
    const json pending = created(&Api::MoneyController::createTransaction,
                                 kAnna,
                                 json{{"date", "2026-10-06"},
                                      {"account_id", kaspi["id"]},
                                      {"amount", 500},
                                      {"category_id", food["id"]},
                                      {"merchant", "Big C"},
                                      {"name", "Big C"},
                                      {"status", "pending"},
                                      {"source", "api"}});
    const json inbox = body_of(call(&Api::MoneyController::inbox, user(kAnna), Get))["data"];
    ASSERT_EQ(inbox.size(), 1u);
    EXPECT_EQ(inbox[0]["possible_duplicate"], true);
    EXPECT_EQ(call_id(&Api::MoneyController::confirmTransaction, user(kBoris), Post, pending["id"])->statusCode(),
              k404NotFound);
    EXPECT_EQ(call_id(&Api::MoneyController::confirmTransaction, user(kAnna), Post, pending["id"])->statusCode(),
              k200OK);
    EXPECT_EQ(body_of(call(&Api::MoneyController::inbox, user(kAnna), Get))["count"], 0);

    const json m = body_of(call(&Api::MoneyController::merchants, user(kAnna), Get, {{"q", "big"}}))["data"];
    ASSERT_EQ(m.size(), 1u);
    EXPECT_EQ(m[0]["times"], 2);
    EXPECT_EQ(m[0]["category_id"], food["id"]);
    EXPECT_EQ(
        call_id(
            &Api::MoneyController::patchMerchant, user(kAnna), Patch, "big c", json{{"category_id", boris_food["id"]}})
            ->statusCode(),
        k400BadRequest)
        << "another user's category";
    EXPECT_EQ(
        call_id(&Api::MoneyController::patchMerchant, user(kBoris), Patch, "big c", json{{"category_id", nullptr}})
            ->statusCode(),
        k404NotFound);
}

TEST_F(MoneyApiTest, ReportIsPerCurrencyWithBudgetsAndTheAsIfBlock) {
    const json kaspi = account(kAnna, "Kaspi", "KZT");
    const json cash = account(kAnna, "Cash THB", "THB");
    const json food = category(kAnna, json{{"name", "Food"}, {"budget_max", 20000}, {"budget_currency", "KZT"}});
    const json rent = category(kAnna, json{{"name", "Rent"}, {"flexibility", "fixed"}});
    expense(kaspi, food, 10000, "2026-09-05", "Big C");
    expense(kaspi, food, 15000, "2026-10-05", "Big C");
    expense(kaspi, rent, 200000, "2026-10-01", "Landlord");
    expense(cash, food, 955.75, "2026-10-06", "Market");

    const auto resp =
        call(&Api::MoneyController::periodReport, user(kAnna), Get, {{"kind", "month"}, {"date", "2026-10-15"}});
    ASSERT_EQ(resp->statusCode(), k200OK) << resp->body();
    const json r = body_of(resp)["data"];
    EXPECT_EQ(r["period"]["from"], "2026-10-01");
    EXPECT_EQ(r["period"]["to"], "2026-10-31");
    ASSERT_EQ(r["blocks"].size(), 2u);
    const json kzt = r["blocks"][0];
    EXPECT_EQ(kzt["currency"], "KZT");
    EXPECT_DOUBLE_EQ(kzt["expense"].get<double>(), 215000);
    EXPECT_DOUBLE_EQ(kzt["fixed_expense"].get<double>(), 200000);
    EXPECT_DOUBLE_EQ(kzt["prev_expense"].get<double>(), 10000);
    const json cats = kzt["categories"];
    ASSERT_EQ(cats.size(), 2u);
    EXPECT_EQ(cats[1]["category_id"], food["id"]);
    EXPECT_DOUBLE_EQ(cats[1]["budget_share"].get<double>(), 0.75);
    EXPECT_EQ(r["blocks"][1]["currency"], "THB");
    EXPECT_TRUE(r["blocks"][1]["categories"][0]["budget"].is_null()) << "the budget is in KZT";
    EXPECT_FALSE(r.contains("as_if"));
    EXPECT_EQ(r["new_merchants"], json::array({"landlord", "market"}));

    // As if in KZT: THB converted with the stored rate on or before the period's end.
    Repositories::Money::FxRateRepository().put_day("2026-10-03", {{"KZT", 450}, {"THB", 33.5}});
    const json a = body_of(call(&Api::MoneyController::periodReport,
                                user(kAnna),
                                Get,
                                {{"kind", "month"}, {"date", "2026-10-15"}, {"as_if", "KZT"}}))["data"]["as_if"];
    EXPECT_EQ(a["currency"], "KZT");
    EXPECT_FALSE(a["partial"]);
    EXPECT_NEAR(a["expense"].get<double>(), 215000 + 955.75 * 450 / 33.5, 1e-6);
    EXPECT_EQ(a["blocks"][1]["rate_date"], "2026-10-03");

    // A currency without a rate makes the block partial instead of guessing.
    const json usd = account(kAnna, "Wise USD", "USD");
    expense(usd, food, 9.99, "2026-10-07", "Apple");
    const json b = body_of(call(&Api::MoneyController::periodReport,
                                user(kAnna),
                                Get,
                                {{"kind", "month"}, {"date", "2026-10-15"}, {"as_if", "EUR"}}))["data"]["as_if"];
    EXPECT_TRUE(b["partial"]) << "no EUR rate stored";

    EXPECT_EQ(call(&Api::MoneyController::periodReport,
                   user(kAnna),
                   Get,
                   {{"kind", "custom"}, {"from", "2026-10-10"}, {"to", "2026-10-01"}})
                  ->statusCode(),
              k400BadRequest);
    EXPECT_EQ(call(&Api::MoneyController::periodReport, user(kAnna), Get, {{"kind", "year"}})->statusCode(),
              k400BadRequest);
    EXPECT_TRUE(body_of(call(
        &Api::MoneyController::periodReport, user(kBoris), Get, {{"date", "2026-10-15"}}))["data"]["blocks"]
                    .empty());
}

TEST_F(MoneyApiTest, RatesAndConversion) {
    Repositories::Money::FxRateRepository().put_day("2026-10-02", {{"KZT", 450}, {"THB", 33.5}});
    const json r = body_of(
        call(&Api::MoneyController::rate, user(kAnna), Get, {{"date", "2026-10-04"}, {"quote", "KZT"}}))["data"];
    EXPECT_EQ(r["rate_date"], "2026-10-02") << "Sunday reads the nearest earlier stored day";
    EXPECT_EQ(
        call(&Api::MoneyController::rate, user(kAnna), Get, {{"date", "2026-10-01"}, {"quote", "KZT"}})->statusCode(),
        k404NotFound);
    const json c = body_of(call(&Api::MoneyController::convert,
                                user(kAnna),
                                Get,
                                {{"amount", "100"}, {"from", "THB"}, {"to", "KZT"}, {"date", "2026-10-03"}}))["data"];
    EXPECT_NEAR(c["amount"].get<double>(), 100 * 450 / 33.5, 1e-9);
    EXPECT_EQ(call(&Api::MoneyController::convert, user(kAnna), Get, {{"amount", "x"}, {"from", "THB"}, {"to", "KZT"}})
                  ->statusCode(),
              k400BadRequest);
}

TEST_F(MoneyApiTest, SettingsRoundTripAndValidation) {
    const auto put = [&](const json& b) { return call_json(&Api::MoneyController::putSettings, user(kAnna), b, Put); };
    EXPECT_EQ(put(json{{"advisor_weekday", 9}})->statusCode(), k400BadRequest);
    EXPECT_EQ(put(json{{"advisor_currencies", {"kzt"}}})->statusCode(), k400BadRequest);
    const auto ok = put(json{{"view_currency", "KZT"},
                             {"advisor_enabled", true},
                             {"advisor_weekday", 7},
                             {"advisor_currencies", {"KZT", "THB"}}});
    ASSERT_EQ(ok->statusCode(), k200OK) << ok->body();
    EXPECT_EQ(body_of(call(&Api::MoneyController::getSettings, user(kAnna), Get))["data"]["view_currency"], "KZT");
    EXPECT_TRUE(
        body_of(call(&Api::MoneyController::getSettings, user(kBoris), Get))["data"]["view_currency"].is_null());
}

TEST_F(MoneyApiTest, ReviewFindingsAnswer4xxNot500) {
    const json kaspi = account(kAnna, "Kaspi", "KZT");
    const json cash = account(kAnna, "Cash THB", "THB");
    const json food = category(kAnna, json{{"name", "Food"}});
    const json row{{"date", "2026-10-05"},
                   {"account_id", kaspi["id"]},
                   {"amount", 10},
                   {"category_id", food["id"]},
                   {"name", "x"},
                   {"external_id", "notion-1"}};
    // Explicit nulls read as "unset", not as a crash.
    json nulls = row;
    nulls["type"] = nullptr;
    nulls["status"] = nullptr;
    nulls["source"] = nullptr;
    nulls["external_id"] = nullptr;
    EXPECT_EQ(call_json(&Api::MoneyController::createTransaction, user(kAnna), nulls)->statusCode(), k201Created);
    EXPECT_EQ(call_json(&Api::MoneyController::createAccount,
                        user(kAnna),
                        json{{"name", "y"}, {"currency", "KZT"}, {"kind", nullptr}})
                  ->statusCode(),
              k201Created);
    EXPECT_EQ(call_json(&Api::MoneyController::putSettings, user(kAnna), json::array(), Put)->statusCode(),
              k400BadRequest);
    EXPECT_EQ(call_json(&Api::MoneyController::putSettings, user(kAnna), json{{"advisor_enabled", nullptr}}, Put)
                  ->statusCode(),
              k200OK);

    // A repeated external_id is a conflict, not a server error.
    EXPECT_EQ(call_json(&Api::MoneyController::createTransaction, user(kAnna), row)->statusCode(), k201Created);
    const auto again = call_json(&Api::MoneyController::createTransaction, user(kAnna), row);
    EXPECT_EQ(again->statusCode(), k409Conflict) << again->body();

    // A category in use keeps its kind.
    EXPECT_EQ(call_id(&Api::MoneyController::updateCategory, user(kAnna), Patch, food["id"], json{{"kind", "income"}})
                  ->statusCode(),
              k400BadRequest);

    // A row moves only to an account in its currency.
    const json t = body_of(call_json(&Api::MoneyController::createTransaction, user(kAnna), [&] {
        json b = row;
        b.erase("external_id");
        return b;
    }()))["data"];
    EXPECT_EQ(
        call_id(&Api::MoneyController::updateTransaction, user(kAnna), Patch, t["id"], json{{"account_id", cash["id"]}})
            ->statusCode(),
        k400BadRequest);
    EXPECT_EQ(
        call_id(&Api::MoneyController::updateTransaction, user(kAnna), Patch, t["id"], json{{"receipt_amount", 5}})
            ->statusCode(),
        k400BadRequest)
        << "the receipt's amount without its currency";

    // Confirming a posted row is not a second visit.
    EXPECT_EQ(call_id(&Api::MoneyController::confirmTransaction, user(kAnna), Post, t["id"])->statusCode(),
              k404NotFound);
    EXPECT_EQ(
        call(&Api::MoneyController::convert, user(kAnna), Get, {{"amount", "nan"}, {"from", "THB"}, {"to", "KZT"}})
            ->statusCode(),
        k400BadRequest);
    EXPECT_EQ(
        call(&Api::MoneyController::convert, user(kAnna), Get, {{"amount", "12abc"}, {"from", "THB"}, {"to", "KZT"}})
            ->statusCode(),
        k400BadRequest);
}

TEST_F(MoneyApiTest, AnAccountCreatedBeforeAnyListSeedsTheCurrencies) {
    Database::get().execute_write([](auto& txn) {
        txn.exec("TRUNCATE TABLE money_accounts, money_currencies CASCADE");
        return true;
    });
    EXPECT_EQ(account(kAnna, "Kaspi", "KZT")["currency"], "KZT");
}

TEST_F(MoneyApiTest, BadIdsAndOtherOwnersRows) {
    EXPECT_EQ(call_id(&Api::MoneyController::getTransaction, user(kAnna), Get, "nope")->statusCode(), k400BadRequest);
    EXPECT_EQ(call_id(&Api::MoneyController::getTransaction, user(kAnna), Get, kMissingId)->statusCode(), k404NotFound);
    EXPECT_EQ(call_id(&Api::MoneyController::deleteAccount, user(kAnna), Delete, kMissingId)->statusCode(),
              k404NotFound);
}

namespace {

class MoneyOffTest : public MoneyApiTest {
protected:
    std::string config_file_name() const override { return "money_off_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["money"]["enabled"] = false; }
};

}  // namespace

TEST_F(MoneyOffTest, EveryRouteIs404WhileTheModuleIsOff) {
    EXPECT_EQ(call(&Api::MoneyController::listAccounts, user(kAnna), Get)->statusCode(), k404NotFound);
    EXPECT_EQ(call(&Api::MoneyController::periodReport, user(kAnna), Get)->statusCode(), k404NotFound);
    EXPECT_EQ(call_json(&Api::MoneyController::createTransaction, user(kAnna), json::object())->statusCode(),
              k404NotFound);
}

// ── parse ───────────────────────────────────────────────────────────────────

namespace {

class MoneyParseApiTest : public MoneyApiTest {
protected:
    std::string config_file_name() const override { return "money_parse_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        MoneyApiTest::config_overrides(cfg);
        cfg["jobs"]["enabled"] = true;
        cfg["jobs"]["result_ttl"] = 3600;
        cfg["money"]["llm"]["base_url"] = "https://llm.example/v1";
        cfg["money"]["llm"]["api_key"] = "sk-test";
        cfg["money"]["llm"]["model"] = "gpt-6-luna";
        cfg["money"]["llm"]["prompt_parse"] = "Answer with JSON.";
    }

    void SetUp() override {
        MoneyApiTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE money_parse_jobs");
            return true;
        });
    }

    /// A finished parse job of Anna with one proposed line.
    std::string done_job(const json& account, const json& category) {
        Repositories::Money::ParseJobRepository jobs;
        const std::string id = jobs.create_text(kAnna, "Kaspi *8880 13275.61 KZT", "2026-10-07")["id"];
        jobs.finish(id,
                    json::array({json{{"date", "2026-10-05"},
                                      {"account_id", account["id"]},
                                      {"amount", 13275.61},
                                      {"name", "Groceries"},
                                      {"category_id", category["id"]}}}),
                    "gpt-6-luna",
                    1,
                    1);
        return id;
    }
};

std::string png_data_url(std::size_t payload_chars) {
    return "data:image/png;base64," + std::string(payload_chars, 'A');
}

}  // namespace

TEST_F(MoneyParseApiTest, TextAndReceiptAreQueuedForTheCaller) {
    EXPECT_EQ(body_of(call(&Api::MoneyController::getSettings, user(kAnna), Get))["data"]["llm_available"], true);
    const auto text = call_json(&Api::MoneyController::parseText, user(kAnna), json{{"text", "Kaspi *8880 100 KZT"}});
    ASSERT_EQ(text->statusCode(), k202Accepted) << text->body();
    const std::string id = body_of(text)["data"]["id"];
    auto job = Jobs::get().pick({"money_parse"}, 1);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->payload["job_id"], id);
    EXPECT_EQ(job->payload["owner_id"], kAnna);
    Jobs::get().complete(job->id, json::object());

    const auto receipt = call_json(&Api::MoneyController::parseReceipt, user(kAnna), json{{"image", png_data_url(16)}});
    ASSERT_EQ(receipt->statusCode(), k202Accepted) << receipt->body();
    const json row =
        body_of(call_id(&Api::MoneyController::parseStatus, user(kAnna), Get, body_of(receipt)["data"]["id"]))["data"];
    EXPECT_EQ(row["kind"], "receipt");
    EXPECT_FALSE(row.contains("image")) << "the photo never leaves";
    EXPECT_EQ(call_id(&Api::MoneyController::parseStatus, user(kBoris), Get, id)->statusCode(), k404NotFound);
    auto second = Jobs::get().pick({"money_parse"}, 1);
    ASSERT_TRUE(second.has_value());
    Jobs::get().complete(second->id, json::object());
}

TEST_F(MoneyParseApiTest, BadImagesAreRefusedBeforeAnythingIsQueued) {
    const auto post = [&](const std::string& image) {
        return call_json(&Api::MoneyController::parseReceipt, user(kAnna), json{{"image", image}})->statusCode();
    };
    EXPECT_EQ(post("aGVsbG8="), k400BadRequest) << "not a data URL";
    EXPECT_EQ(post("data:image/gif;base64,aGVsbG8="), k400BadRequest);
    EXPECT_EQ(post("data:image/png,aGVsbG8="), k400BadRequest) << "not base64";
    EXPECT_EQ(post("data:image/png;base64,aGVs*G8="), k400BadRequest);
    EXPECT_EQ(post("data:image/png;base64,aGVsbG8"), k400BadRequest) << "length not a multiple of 4";
    EXPECT_EQ(post(png_data_url(4 * 1024 * 1024 / 3 * 4 + 8)), k400BadRequest) << "over 4 MB decoded";
    EXPECT_EQ(call_json(&Api::MoneyController::parseText, user(kAnna), json{{"text", ""}})->statusCode(),
              k400BadRequest);
    EXPECT_EQ(call_json(&Api::MoneyController::parseText, user(kAnna), json{{"text", "x"}, {"hint_date", "2026-02-30"}})
                  ->statusCode(),
              k400BadRequest);
    // Nothing was created, so nothing was queued (an empty queue is not picked from:
    // the test Redis answers a blocking pop on an empty list with a socket timeout).
    const long rows = Database::get().execute_read(
        [](auto& txn) { return txn.exec("SELECT count(*) FROM money_parse_jobs")[0][0].template as<long>(); });
    EXPECT_EQ(rows, 0);
}

TEST_F(MoneyParseApiTest, AcceptPutsTheEditedLinesInTheInboxOnce) {
    const json kaspi = account(kAnna, "Kaspi", "KZT");
    const json food = category(kAnna, json{{"name", "Food"}});
    const std::string id = done_job(kaspi, food);
    const json line{{"date", "2026-10-05"},
                    {"account_id", kaspi["id"]},
                    {"amount", 13275.61},
                    {"name", "Groceries (store)"},
                    {"category_id", food["id"]},
                    {"status", "posted"}};

    json no_account = line;
    no_account.erase("account_id");
    EXPECT_EQ(
        call_id(&Api::MoneyController::parseAccept, user(kAnna), Post, id, json{{"lines", {no_account}}})->statusCode(),
        k400BadRequest)
        << "a line without an account cannot be accepted";
    EXPECT_EQ(
        call_id(&Api::MoneyController::parseAccept, user(kBoris), Post, id, json{{"lines", {line}}})->statusCode(),
        k404NotFound);

    const auto ok = call_id(&Api::MoneyController::parseAccept, user(kAnna), Post, id, json{{"lines", {line}}});
    ASSERT_EQ(ok->statusCode(), k201Created) << ok->body();
    const json rows = body_of(ok)["data"];
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0]["status"], "pending") << "a parse never posts";
    EXPECT_EQ(rows[0]["source"], "text");
    EXPECT_EQ(body_of(call(&Api::MoneyController::inbox, user(kAnna), Get))["count"], 1);

    EXPECT_EQ(call_id(&Api::MoneyController::parseAccept, user(kAnna), Post, id, json{{"lines", {line}}})->statusCode(),
              k409Conflict);
    EXPECT_EQ(body_of(call(&Api::MoneyController::inbox, user(kAnna), Get))["count"], 1) << "nothing written twice";
}

TEST_F(MoneyParseApiTest, AFewOpenParsesPerUser) {
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(call_json(&Api::MoneyController::parseText, user(kAnna), json{{"text", "x"}})->statusCode(),
                  k202Accepted);
    }
    EXPECT_EQ(call_json(&Api::MoneyController::parseText, user(kAnna), json{{"text", "x"}})->statusCode(),
              k429TooManyRequests);
    EXPECT_EQ(call_json(&Api::MoneyController::parseText, user(kBoris), json{{"text", "x"}})->statusCode(),
              k202Accepted);
    for (int i = 0; i < 4; ++i) {  // exactly what was queued: three of Anna's, one of Boris's
        auto job = Jobs::get().pick({"money_parse"}, 1);
        ASSERT_TRUE(job.has_value());
        Jobs::get().complete(job->id, json::object());
    }
}

// ── advisor ─────────────────────────────────────────────────────────────────

namespace {

class MoneyAdvisorApiTest : public MoneyApiTest {
protected:
    std::string config_file_name() const override { return "money_advisor_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        MoneyApiTest::config_overrides(cfg);
        cfg["jobs"]["enabled"] = true;
        cfg["jobs"]["result_ttl"] = 3600;
        cfg["money"]["llm"]["base_url"] = "https://llm.example/v1";
        cfg["money"]["llm"]["api_key"] = "sk-test";
        cfg["money"]["llm"]["model"] = "gpt-6-luna";
        cfg["money"]["llm"]["prompt_advisor"] = "Write a review.";
    }
    void SetUp() override {
        MoneyApiTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE money_advisor_reports");
            return true;
        });
    }
};

}  // namespace

TEST_F(MoneyAdvisorApiTest, RunQueuesOnceAndTheReportsStayWithTheirOwner) {
    const auto run = [&](const json& body) { return call_json(&Api::MoneyController::advisorRun, user(kAnna), body); };
    const auto first = run(json{{"period", "month"}, {"date", "2026-10-07"}});
    ASSERT_EQ(first->statusCode(), k202Accepted) << first->body();
    const json row = body_of(first)["data"];
    EXPECT_EQ(row["period_start"], "2026-10-01");
    EXPECT_EQ(row["period_end"], "2026-10-31");
    const auto again = run(json{{"period", "month"}, {"date", "2026-10-20"}});
    EXPECT_EQ(again->statusCode(), k200OK) << "the same month is not queued twice";
    EXPECT_EQ(body_of(again)["queued"], false);
    auto job = Jobs::get().pick({"money_advisor"}, 1);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->payload["report_id"], row["id"]);
    Jobs::get().complete(job->id, json::object());

    EXPECT_EQ(body_of(call(&Api::MoneyController::advisorReports, user(kAnna), Get))["count"], 1);
    EXPECT_EQ(body_of(call(&Api::MoneyController::advisorReports, user(kBoris), Get))["count"], 0);
    EXPECT_EQ(call_id(&Api::MoneyController::advisorReport, user(kBoris), Get, row["id"])->statusCode(), k404NotFound);
    EXPECT_EQ(call_id(&Api::MoneyController::advisorReport, user(kAnna), Get, row["id"])->statusCode(), k200OK);
    EXPECT_EQ(run(json{{"period", "year"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(run(json{{"date", "2026-02-30"}})->statusCode(), k400BadRequest);
}
