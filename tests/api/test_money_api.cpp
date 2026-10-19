/**
 * @file test_money_api.cpp
 * @brief Money module routes: two users apart, the invariants as 400s,
 *        balances through transfers, reports per currency with the "as if"
 *        block, rates, settings, the module switch.
 */

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "InMemoryCache.hpp"
#include "api/MoneyController.hpp"
#include "cache/Cache.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "jobs/Jobs.hpp"
#include "jobs/Outbox.hpp"
#include "money/DigestCache.hpp"
#include "money/Period.hpp"
#include "repositories/money/FxRateRepository.hpp"
#include "repositories/money/ParseJobRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa7";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb7";
constexpr const char* kMissingId = "55555555-5555-4555-8555-555555555555";

using TestHelpers::user;

using TestHelpers::body_of;

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

    // The spec path of each handler the suite drives, for the schema check
    // in call* (tests/test_helpers.hpp expect_2xx_matches_schema); a handler
    // missing here is not checked.
    static const char* spec_path_of(Handler h) {
        static const std::pair<Handler, const char*> kPaths[] = {
            {&Api::MoneyController::listCurrencies, "/api/v1/money/currencies"},
            {&Api::MoneyController::upsertCurrency, "/api/v1/money/currencies"},
            {&Api::MoneyController::listAccounts, "/api/v1/money/accounts"},
            {&Api::MoneyController::createAccount, "/api/v1/money/accounts"},
            {&Api::MoneyController::listCategories, "/api/v1/money/categories"},
            {&Api::MoneyController::createCategory, "/api/v1/money/categories"},
            {&Api::MoneyController::listTransactions, "/api/v1/money/transactions"},
            {&Api::MoneyController::createTransaction, "/api/v1/money/transactions"},
            {&Api::MoneyController::createTransactions, "/api/v1/money/transactions/batch"},
            {&Api::MoneyController::inbox, "/api/v1/money/inbox"},
            {&Api::MoneyController::listTransfers, "/api/v1/money/transfers"},
            {&Api::MoneyController::createTransfer, "/api/v1/money/transfers"},
            {&Api::MoneyController::merchants, "/api/v1/money/merchants"},
            {&Api::MoneyController::patchMerchant, "/api/v1/money/merchants"},
            {&Api::MoneyController::rate, "/api/v1/money/rates"},
            {&Api::MoneyController::convert, "/api/v1/money/rates/convert"},
            {&Api::MoneyController::refreshRates, "/api/v1/money/rates/refresh"},
            {&Api::MoneyController::periodReport, "/api/v1/money/reports/period"},
            {&Api::MoneyController::balances, "/api/v1/money/reports/balances"},
            {&Api::MoneyController::summary, "/api/v1/money/summary"},
            {&Api::MoneyController::parseText, "/api/v1/money/parse"},
            {&Api::MoneyController::parseReceipt, "/api/v1/money/parse/receipt"},
            {&Api::MoneyController::advisorReports, "/api/v1/money/advisor/reports"},
            {&Api::MoneyController::advisorRun, "/api/v1/money/advisor/run"},
            {&Api::MoneyController::getSettings, "/api/v1/money/settings"},
            {&Api::MoneyController::putSettings, "/api/v1/money/settings"},
        };
        for (const auto& [handler, path] : kPaths)
            if (handler == h)
                return path;
        return nullptr;
    }
    static const char* spec_path_of(IdHandler h) {
        static const std::pair<IdHandler, const char*> kPaths[] = {
            {&Api::MoneyController::patchCurrency, "/api/v1/money/currencies/{code}"},
            {&Api::MoneyController::getAccount, "/api/v1/money/accounts/{id}"},
            {&Api::MoneyController::updateAccount, "/api/v1/money/accounts/{id}"},
            {&Api::MoneyController::deleteAccount, "/api/v1/money/accounts/{id}"},
            {&Api::MoneyController::getCategory, "/api/v1/money/categories/{id}"},
            {&Api::MoneyController::updateCategory, "/api/v1/money/categories/{id}"},
            {&Api::MoneyController::deleteCategory, "/api/v1/money/categories/{id}"},
            {&Api::MoneyController::getTransaction, "/api/v1/money/transactions/{id}"},
            {&Api::MoneyController::updateTransaction, "/api/v1/money/transactions/{id}"},
            {&Api::MoneyController::deleteTransaction, "/api/v1/money/transactions/{id}"},
            {&Api::MoneyController::confirmTransaction, "/api/v1/money/transactions/{id}/confirm"},
            {&Api::MoneyController::getTransfer, "/api/v1/money/transfers/{id}"},
            {&Api::MoneyController::updateTransfer, "/api/v1/money/transfers/{id}"},
            {&Api::MoneyController::deleteTransfer, "/api/v1/money/transfers/{id}"},
            {&Api::MoneyController::parseStatus, "/api/v1/money/parse/{id}"},
            {&Api::MoneyController::parseAccept, "/api/v1/money/parse/{id}/accept"},
            {&Api::MoneyController::advisorReport, "/api/v1/money/advisor/reports/{id}"},
        };
        for (const auto& [handler, path] : kPaths)
            if (handler == h)
                return path;
        return nullptr;
    }

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
        if (const char* path = spec_path_of(h))
            TestHelpers::expect_2xx_matches_schema(captured, m, path);
        return captured;
    }

    HttpResponsePtr call_json(Handler h,
                              const Security::Auth::AuthPrincipal& p,
                              const json& body,
                              HttpMethod m = Post) {
        HttpResponsePtr captured;
        (controller.*h)(TestHelpers::authed_json(p, body, m), [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        if (const char* path = spec_path_of(h))
            TestHelpers::expect_2xx_matches_schema(captured, m, path);
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
        if (const char* path = spec_path_of(h))
            TestHelpers::expect_2xx_matches_schema(captured, m, path);
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
    json categorised = adj;
    categorised["category_id"] = food["id"];
    EXPECT_EQ(post(categorised), k400BadRequest) << "an adjustment has no category";

    const json row = body_of(call_id(&Api::MoneyController::getTransaction, user(kAnna), Get, t1["id"]))["data"];
    EXPECT_DOUBLE_EQ(row["final_amount"].get<double>(), 13255.61);
    EXPECT_EQ(row["adjustments"].size(), 1u);
    EXPECT_EQ(row["adjustments"][0]["status"], "posted");

    // A pending adjustment waits in the inbox: listed, not yet in the final amount.
    json waiting = adj;
    waiting["amount"] = 50;
    waiting["status"] = "pending";
    const auto queued = call_json(&Api::MoneyController::createTransaction, user(kAnna), waiting);
    ASSERT_EQ(queued->statusCode(), k201Created) << queued->body();
    const json listed = body_of(call_id(&Api::MoneyController::getTransaction, user(kAnna), Get, t1["id"]))["data"];
    EXPECT_DOUBLE_EQ(listed["final_amount"].get<double>(), 13255.61) << "the pending +50 is not in yet";
    ASSERT_EQ(listed["adjustments"].size(), 2u);
    EXPECT_EQ(listed["adjustments"][1]["status"], "pending");
    ASSERT_EQ(call_id(&Api::MoneyController::confirmTransaction, user(kAnna), Post, body_of(queued)["data"]["id"])
                  ->statusCode(),
              k200OK);
    EXPECT_DOUBLE_EQ(
        body_of(call_id(&Api::MoneyController::getTransaction, user(kAnna), Get, t1["id"]))["data"]["final_amount"]
            .get<double>(),
        13305.61);
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

    const json page = body_of(call(&Api::MoneyController::listTransfers, user(kAnna), Get, {{"limit", "1"}}));
    EXPECT_EQ(page["data"].size(), 1u);
    EXPECT_EQ(page["count"], 1);
    EXPECT_EQ(page["total"], 2) << "total counts past the page, so a client can page";
    EXPECT_EQ(body_of(call(&Api::MoneyController::listTransfers, user(kAnna), Get, {{"offset", "1"}}))["count"], 1);
    EXPECT_EQ(body_of(call(&Api::MoneyController::listTransfers, user(kBoris), Get))["total"], 0);
}

TEST_F(MoneyApiTest, WithoutADateTheRateIsOfTodayInTheGivenZone) {
    // Kiritimati is UTC+14 and Pago Pago UTC-11: at any hour one of them is on another date than UTC.
    const auto [zone, local, utc] = Database::get().execute_read([](auto& txn) {
        auto r = txn.exec(
            "SELECT z, to_char(now() AT TIME ZONE z, 'YYYY-MM-DD'), to_char(now() AT TIME ZONE 'UTC', 'YYYY-MM-DD') "
            "FROM unnest(ARRAY['Pacific/Kiritimati', 'Pacific/Pago_Pago']) AS z "
            "WHERE (now() AT TIME ZONE z)::date <> (now() AT TIME ZONE 'UTC')::date LIMIT 1");
        return std::make_tuple(
            r[0][0].template as<std::string>(), r[0][1].template as<std::string>(), r[0][2].template as<std::string>());
    });
    ASSERT_NE(local, utc);
    const std::string local_day = local;
    const std::string utc_day = utc;
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params(
            "INSERT INTO money_fx_rates (date, quote, per_usd) VALUES ($1::date, 'ZZZ', 7), ($2::date, 'ZZZ', 9)",
            local_day,
            utc_day);
        return true;
    });
    const auto rate = [&](const std::vector<std::pair<std::string, std::string>>& params) {
        return body_of(call(&Api::MoneyController::rate, user(kAnna), Get, params))["data"]["rate_date"];
    };
    EXPECT_EQ(rate({{"quote", "ZZZ"}, {"tz", zone}}), local);
    EXPECT_EQ(rate({{"quote", "ZZZ"}}), utc) << "no tz: UTC";
    const auto convert = [&](const std::vector<std::pair<std::string, std::string>>& extra) {
        std::vector<std::pair<std::string, std::string>> params{{"amount", "1"}, {"from", "USD"}, {"to", "ZZZ"}};
        params.insert(params.end(), extra.begin(), extra.end());
        return body_of(call(&Api::MoneyController::convert, user(kAnna), Get, params))["data"]["rate_date"];
    };
    EXPECT_EQ(convert({{"tz", zone}}), local);
    EXPECT_EQ(convert({}), utc) << "no tz: UTC";
    EXPECT_EQ(convert({{"tz", zone}, {"date", utc}}), utc) << "a given date wins";
}

// Every money write replaces the owner's digest token before it answers, so
// the /today the page fetches next computes the money part afresh; a read
// leaves it, and another owner's token is not touched.
TEST_F(MoneyApiTest, AMoneyWriteReplacesTheOwnersDigestToken) {
    Cache::install_for_testing(std::make_unique<TestSupport::InMemoryCache>());
    const auto token = [](const char* who) { return Cache::get().get(Money::DigestCache::owner_version_key(who)); };
    EXPECT_FALSE(token(kAnna).has_value());
    std::optional<std::string> seen_before_answer;
    {
        auto req = TestHelpers::authed_json(user(kAnna), json{{"name", "Kaspi"}, {"currency", "KZT"}}, Post);
        controller.createAccount(req, [&](const HttpResponsePtr&) { seen_before_answer = token(kAnna); });
    }
    ASSERT_TRUE(seen_before_answer.has_value()) << "replaced before the answer went out";
    const auto after_write = token(kAnna);
    call(&Api::MoneyController::listAccounts, user(kAnna), Get);
    EXPECT_EQ(token(kAnna), after_write) << "a read keeps it";
    EXPECT_FALSE(token(kBoris).has_value());
    const json kaspi = body_of(call(&Api::MoneyController::listAccounts, user(kAnna), Get))["data"][0];
    call_id(&Api::MoneyController::updateAccount, user(kAnna), Patch, kaspi["id"], json{{"name", "Kaspi Gold"}});
    EXPECT_NE(token(kAnna), after_write) << "every write replaces it";
    Cache::reset_for_testing();
}

// A tz that is not a zone is refused, as /today and the agenda refuse it,
// instead of quietly filing the day under UTC.
TEST_F(MoneyApiTest, ATzThatIsNotAZoneIsRefused) {
    const std::vector<std::pair<std::string, std::string>> bad{{"tz", "Mars/Olympus"}};
    const auto get = [&](Handler h, std::vector<std::pair<std::string, std::string>> params) {
        params.insert(params.end(), bad.begin(), bad.end());
        return call(h, user(kAnna), Get, params);
    };
    for (const auto& resp : {get(&Api::MoneyController::rate, {{"quote", "KZT"}}),
                             get(&Api::MoneyController::convert, {{"amount", "1"}, {"from", "USD"}, {"to", "KZT"}}),
                             get(&Api::MoneyController::periodReport, {}),
                             get(&Api::MoneyController::summary, {{"currency", "USD"}})}) {
        ASSERT_EQ(resp->statusCode(), k400BadRequest) << resp->body();
        EXPECT_EQ(body_of(resp)["error"], "invalid_query");
    }
    const auto post = [&](Handler h, const json& body) {
        auto req = TestHelpers::authed_json(user(kAnna), body, Post);
        req->setParameter("tz", "Mars/Olympus");
        HttpResponsePtr captured;
        (controller.*h)(req, [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    };
    for (const auto& resp : {post(&Api::MoneyController::parseText, json{{"text", "coffee 1500"}}),
                             post(&Api::MoneyController::advisorRun, json::object())}) {
        ASSERT_TRUE(resp);
        ASSERT_EQ(resp->statusCode(), k400BadRequest) << resp->body();
        EXPECT_EQ(body_of(resp)["error"], "invalid_query");
    }
    EXPECT_EQ(
        call(&Api::MoneyController::rate, user(kAnna), Get, {{"quote", "USD"}, {"tz", "Asia/Almaty"}})->statusCode(),
        k200OK)
        << "a zone passes";
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
    const auto move = [&](const std::string& who, const json& body) {
        return call_json(&Api::MoneyController::patchMerchant, user(who), body, Patch);
    };
    EXPECT_EQ(move(kAnna, json{{"merchant_key", "big c"}, {"category_id", boris_food["id"]}})->statusCode(),
              k400BadRequest)
        << "another user's category";
    EXPECT_EQ(move(kBoris, json{{"merchant_key", "big c"}, {"category_id", nullptr}})->statusCode(), k404NotFound);
    EXPECT_EQ(move(kAnna, json{{"category_id", nullptr}})->statusCode(), k400BadRequest) << "no key";
    EXPECT_EQ(move(kAnna, json{{"merchant_key", "big c"}})->statusCode(), k400BadRequest) << "no category_id";
    const json other = category(kAnna, json{{"name", "Household"}});
    const auto moved = move(kAnna, json{{"merchant_key", "Big  C!"}, {"category_id", other["id"]}});
    ASSERT_EQ(moved->statusCode(), k200OK) << "a display name finds its key: " << moved->body();
    EXPECT_EQ(body_of(moved)["data"]["merchant_key"], "big c");
    EXPECT_EQ(body_of(moved)["data"]["category_id"], other["id"]);
    const auto cleared = move(kAnna, json{{"merchant_key", "big c"}, {"category_id", nullptr}});
    ASSERT_EQ(cleared->statusCode(), k200OK);
    EXPECT_TRUE(body_of(cleared)["data"]["category_id"].is_null());
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

    // A bank recalculation of a Food row counts against the Food budget, and
    // one of a fixed row in the fixed share.
    const json food_row = body_of(call(&Api::MoneyController::listTransactions,
                                       user(kAnna),
                                       Get,
                                       {{"from", "2026-10-05"}, {"to", "2026-10-05"}}))["data"][0];
    const json rent_row = body_of(call(&Api::MoneyController::listTransactions,
                                       user(kAnna),
                                       Get,
                                       {{"from", "2026-10-01"}, {"to", "2026-10-01"}}))["data"][0];
    for (const auto& [orig, amount] : {std::pair{food_row, 1000.0}, std::pair{rent_row, -500.0}}) {
        created(&Api::MoneyController::createTransaction,
                kAnna,
                json{{"type", "fx_adjustment"},
                     {"date", "2026-10-08"},
                     {"account_id", kaspi["id"]},
                     {"amount", amount},
                     {"adjusts_id", orig["id"]},
                     {"name", "Bank recalculation"}});
    }
    const json adjusted = body_of(call(&Api::MoneyController::periodReport,
                                       user(kAnna),
                                       Get,
                                       {{"kind", "month"}, {"date", "2026-10-15"}}))["data"]["blocks"][0];
    EXPECT_DOUBLE_EQ(adjusted["expense"].get<double>(), 215500);
    EXPECT_DOUBLE_EQ(adjusted["fixed_expense"].get<double>(), 199500);
    EXPECT_EQ(adjusted["categories"][1]["category_id"], food["id"]);
    EXPECT_DOUBLE_EQ(adjusted["categories"][1]["spent"].get<double>(), 16000);
    EXPECT_DOUBLE_EQ(adjusted["categories"][1]["budget_share"].get<double>(), 0.8);

    // As if in KZT: THB converted with the stored rate on or before the period's end.
    Repositories::Money::FxRateRepository().put_day("2026-10-03", {{"KZT", 450}, {"THB", 33.5}});
    const json a = body_of(call(&Api::MoneyController::periodReport,
                                user(kAnna),
                                Get,
                                {{"kind", "month"}, {"date", "2026-10-15"}, {"as_if", "KZT"}}))["data"]["as_if"];
    EXPECT_EQ(a["currency"], "KZT");
    EXPECT_FALSE(a["partial"]);
    EXPECT_NEAR(a["expense"].get<double>(), 215500 + 955.75 * 450 / 33.5, 1e-6) << "the two adjustments above are in";
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

// ── eight decimals ──────────────────────────────────────────────────────────

TEST_F(MoneyApiTest, AnAmountADoubleCannotHoldIsRefused) {
    const json kaspi = account(kAnna, "Kaspi", "KZT");
    const json food = category(kAnna, json{{"name", "Food"}});
    const auto post = [&](double amount) {
        return call_json(&Api::MoneyController::createTransaction,
                         user(kAnna),
                         json{{"date", "2026-10-05"},
                              {"account_id", kaspi["id"]},
                              {"amount", amount},
                              {"category_id", food["id"]},
                              {"name", "Groceries"}});
    };
    // 16 significant digits: the double nearest to it is not the amount sent.
    const auto refused = post(12345678901.23456);
    ASSERT_EQ(refused->statusCode(), k400BadRequest) << refused->body();
    const json errors = body_of(refused)["errors"];
    ASSERT_EQ(errors.size(), 1u) << errors.dump();
    EXPECT_EQ(errors[0]["field"], "amount");
    EXPECT_EQ(errors[0]["code"], "precision");
    // 15 digits are kept exactly.
    EXPECT_EQ(post(123456789012.34)->statusCode(), k201Created);
    EXPECT_EQ(post(13275.61)->statusCode(), k201Created);
    EXPECT_EQ(call_json(&Api::MoneyController::createAccount,
                        user(kAnna),
                        json{{"name", "Big"}, {"currency", "KZT"}, {"opening_balance", 0.1 + 0.2}})
                  ->statusCode(),
              k400BadRequest)
        << "0.30000000000000004 is 17 digits";
}

TEST_F(MoneyApiTest, ACryptoWalletKeepsEightDecimals) {
    // A known code takes its smallest unit from the table; it cannot be told otherwise.
    const auto btc =
        call_json(&Api::MoneyController::upsertCurrency, user(kAnna), json{{"code", "BTC"}, {"name", "Bitcoin"}});
    ASSERT_EQ(btc->statusCode(), k200OK) << btc->body();
    EXPECT_EQ(body_of(btc)["data"]["decimals"], 8);
    EXPECT_EQ(body_of(btc)["data"]["minor_unit"], "satoshi");
    EXPECT_EQ(call_json(&Api::MoneyController::upsertCurrency, user(kAnna), json{{"code", "JPY"}, {"decimals", 2}})
                  ->statusCode(),
              k400BadRequest);
    EXPECT_EQ(
        call_id(&Api::MoneyController::patchCurrency, user(kAnna), Patch, "KZT", json{{"decimals", 3}})->statusCode(),
        k400BadRequest);
    // An unknown code names its own.
    EXPECT_EQ(call_json(&Api::MoneyController::upsertCurrency, user(kAnna), json{{"code", "XAU"}})->statusCode(),
              k400BadRequest);
    const auto xau = call_json(&Api::MoneyController::upsertCurrency,
                               user(kAnna),
                               json{{"code", "XAU"}, {"decimals", 3}, {"minor_unit", "milli-ounce"}});
    ASSERT_EQ(xau->statusCode(), k200OK) << xau->body();
    EXPECT_EQ(body_of(xau)["data"]["minor_unit"], "milli-ounce");
    const json boris_currencies = body_of(call(&Api::MoneyController::listCurrencies, user(kBoris), Get))["data"];
    for (const auto& c : boris_currencies) {
        if (c["code"] == "RUB") {
            EXPECT_EQ(c["minor_unit"], "kopeck") << "seeded with its unit";
        }
    }
    EXPECT_EQ(call_json(&Api::MoneyController::upsertCurrency, user(kAnna), json{{"code", "XAU"}, {"decimals", 9}})
                  ->statusCode(),
              k400BadRequest);
    const json kaspi = account(kAnna, "Kaspi", "KZT", 300000);
    const json wallet = account(kAnna, "Telegram Wallet BTC", "BTC");
    const json fees = category(kAnna, json{{"name", "Fees"}});
    const auto x = call_json(&Api::MoneyController::createTransfer,
                             user(kAnna),
                             json{{"date", "2026-09-25"},
                                  {"from_account_id", kaspi["id"]},
                                  {"to_account_id", wallet["id"]},
                                  {"amount_sent", 230000},
                                  {"amount_received", 0.0057635}});
    ASSERT_EQ(x->statusCode(), k201Created) << x->body();
    created(&Api::MoneyController::createTransaction,
            kAnna,
            json{{"date", "2026-09-26"},
                 {"account_id", wallet["id"]},
                 {"amount", 0.00001234},
                 {"category_id", fees["id"]},
                 {"name", "Network fee (BTC)"}});
    EXPECT_NEAR(balance_of(wallet), 0.0057635 - 0.00001234, 1e-9);
    EXPECT_NEAR(balance_of(kaspi), 300000 - 230000, 0.005);
    // Finer than the smallest unit is refused: a tiyn is 0.01 KZT.
    const auto fine = call_json(&Api::MoneyController::createTransaction,
                                user(kAnna),
                                json{{"date", "2026-09-26"},
                                     {"account_id", kaspi["id"]},
                                     {"amount", 1.234},
                                     {"category_id", fees["id"]},
                                     {"name", "Too fine"}});
    EXPECT_EQ(fine->statusCode(), k400BadRequest);
    EXPECT_NE(std::string(fine->body()).find("tiyn"), std::string::npos) << fine->body();
    EXPECT_EQ(call_json(&Api::MoneyController::createTransfer,
                        user(kAnna),
                        json{{"date", "2026-09-26"},
                             {"from_account_id", kaspi["id"]},
                             {"to_account_id", wallet["id"]},
                             {"amount_sent", 100},
                             {"amount_received", 0.000000001}})
                  ->statusCode(),
              k400BadRequest)
        << "below one satoshi";
    const auto satoshi = call_json(&Api::MoneyController::createTransfer,
                                   user(kAnna),
                                   json{{"date", "2026-09-26"},
                                        {"from_account_id", wallet["id"]},
                                        {"to_account_id", kaspi["id"]},
                                        {"amount_sent", 0.00000001},
                                        {"amount_received", 0.01}});
    EXPECT_EQ(satoshi->statusCode(), k201Created) << "one satoshi is a real amount: " << satoshi->body();
    EXPECT_EQ(
        call_id(
            &Api::MoneyController::updateAccount, user(kAnna), Patch, kaspi["id"], json{{"opening_balance", 10.005}})
            ->statusCode(),
        k400BadRequest);
}

TEST_F(MoneyApiTest, RatesBackfillChecksTheRangeBeforeTheQueue) {
    const auto post = [&](const json& b) {
        return call_json(&Api::MoneyController::refreshRates, user(kAnna), b)->statusCode();
    };
    EXPECT_EQ(post(json{{"from", "2024-01-01"}}), k400BadRequest) << "the source starts on 2024-03-02";
    EXPECT_EQ(post(json{{"from", "2026-10-05"}, {"to", "2026-10-01"}}), k400BadRequest);
    EXPECT_EQ(post(json{{"from", "2026-02-30"}}), k400BadRequest);
    EXPECT_EQ(post(json{{"to", "2026-10-01"}}), k400BadRequest) << "from is required with a body";
    // Both days stored already: whether a queue runs in this binary or not,
    // a valid range passes the checks and nothing new is queued.
    Repositories::Money::FxRateRepository().put_day("2026-10-01", {{"KZT", 450}});
    Repositories::Money::FxRateRepository().put_day("2026-10-02", {{"KZT", 450}});
    const auto ok =
        call_json(&Api::MoneyController::refreshRates, user(kAnna), json{{"from", "2026-10-01"}, {"to", "2026-10-02"}});
    if (ok->statusCode() == k202Accepted) {
        EXPECT_EQ(body_of(ok)["data"]["queued"], 0);
    } else {
        EXPECT_EQ(ok->statusCode(), k503ServiceUnavailable) << ok->body();
    }
}

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
        EXPECT_EQ(jobs.start(id), 1) << "the worker claims it before it answers";
        jobs.finish(id,
                    /*attempt=*/1,
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

TEST_F(MoneyParseApiTest, WithoutAHintDateTheParseTakesTodayInTheGivenZone) {
    // Kiritimati is UTC+14 and Pago Pago UTC-11: at any hour one of them is on another date than UTC.
    const auto [zone, local, utc] = Database::get().execute_read([](auto& txn) {
        auto r = txn.exec(
            "SELECT z, to_char(now() AT TIME ZONE z, 'YYYY-MM-DD'), to_char(now() AT TIME ZONE 'UTC', 'YYYY-MM-DD') "
            "FROM unnest(ARRAY['Pacific/Kiritimati', 'Pacific/Pago_Pago']) AS z "
            "WHERE (now() AT TIME ZONE z)::date <> (now() AT TIME ZONE 'UTC')::date LIMIT 1");
        return std::make_tuple(
            r[0][0].template as<std::string>(), r[0][1].template as<std::string>(), r[0][2].template as<std::string>());
    });
    ASSERT_NE(local, utc);
    auto req = TestHelpers::authed_json(user(kAnna), json{{"text", "Kaspi *8880 100 KZT"}});
    req->setParameter("tz", zone);
    HttpResponsePtr resp;
    controller.parseText(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_TRUE(resp);
    ASSERT_EQ(resp->statusCode(), k202Accepted) << resp->body();
    TestHelpers::expect_matches_schema(resp, "POST", "/api/v1/money/parse");
    const json row =
        body_of(call_id(&Api::MoneyController::parseStatus, user(kAnna), Get, body_of(resp)["data"]["id"]))["data"];
    EXPECT_EQ(row["hint_date"], local);
    auto job = Jobs::get().pick({"money_parse"}, 1);
    ASSERT_TRUE(job.has_value());
    Jobs::get().complete(job->id, json::object());
}

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
    const auto theirs = call_id(&Api::MoneyController::parseStatus, user(kBoris), Get, id);
    EXPECT_EQ(theirs->statusCode(), k404NotFound);
    EXPECT_EQ(body_of(theirs)["message"], "parse_job not found") << "one name for a missing parse job";
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

TEST_F(MoneyParseApiTest, TodaysRatesAreQueuedOnce) {
    const auto first = call_json(&Api::MoneyController::refreshRates, user(kAnna), json::object());
    ASSERT_EQ(first->statusCode(), k202Accepted) << first->body();
    EXPECT_EQ(body_of(first)["data"]["status"], "queued");
    const auto again = call_json(&Api::MoneyController::refreshRates, user(kAnna), json::object());
    ASSERT_EQ(again->statusCode(), k202Accepted);
    EXPECT_EQ(body_of(again)["data"]["status"], "already_queued") << "the waiting job covers it";
    auto job = Jobs::get().pick({"money_rates"}, 1);
    ASSERT_TRUE(job.has_value());
    Jobs::get().complete(job->id, json::object());
}

TEST_F(MoneyParseApiTest, ASecondBackfillWaitsForTheFirst) {
    const auto post = [&](const json& b) { return call_json(&Api::MoneyController::refreshRates, user(kAnna), b); };
    const auto first = post(json{{"from", "2024-04-01"}, {"to", "2024-04-02"}});
    ASSERT_EQ(first->statusCode(), k202Accepted) << first->body();
    EXPECT_EQ(body_of(first)["data"]["queued"], 2);
    const auto again = post(json{{"from", "2024-04-01"}, {"to", "2024-04-02"}});
    EXPECT_EQ(again->statusCode(), k409Conflict) << "the same days would be queued twice";
    EXPECT_EQ(body_of(again)["error"], "backfill_running");
    // A range with nothing missing queues nothing, so it is not refused.
    Repositories::Money::FxRateRepository().put_day("2024-05-01", {{"KZT", 450}});
    const auto stored = post(json{{"from", "2024-05-01"}, {"to", "2024-05-01"}});
    ASSERT_EQ(stored->statusCode(), k202Accepted) << stored->body();
    EXPECT_EQ(body_of(stored)["data"]["queued"], 0);
    for (int i = 0; i < 2; ++i) {
        auto job = Jobs::get().pick({"money_rates"}, 1);
        ASSERT_TRUE(job.has_value());
        Jobs::get().complete(job->id, json::object());
    }
    EXPECT_EQ(post(json{{"from", "2024-04-01"}, {"to", "2024-04-02"}})->statusCode(), k202Accepted)
        << "the queue is empty again; the days still have no rates";
    for (int i = 0; i < 2; ++i) {
        auto job = Jobs::get().pick({"money_rates"}, 1);
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

TEST_F(MoneyAdvisorApiTest, WithoutADateTheRunIsForThePeriodOfTodayInTheGivenZone) {
    // Kiritimati is UTC+14 and Pago Pago UTC-11: at any hour one of them is on another date than UTC.
    const auto [zone, local, utc] = Database::get().execute_read([](auto& txn) {
        auto r = txn.exec(
            "SELECT z, to_char(now() AT TIME ZONE z, 'YYYY-MM-DD'), to_char(now() AT TIME ZONE 'UTC', 'YYYY-MM-DD') "
            "FROM unnest(ARRAY['Pacific/Kiritimati', 'Pacific/Pago_Pago']) AS z "
            "WHERE (now() AT TIME ZONE z)::date <> (now() AT TIME ZONE 'UTC')::date LIMIT 1");
        return std::make_tuple(
            r[0][0].template as<std::string>(), r[0][1].template as<std::string>(), r[0][2].template as<std::string>());
    });
    ASSERT_NE(local, utc);
    auto req = TestHelpers::authed_json(user(kAnna), json{{"period", "week"}}, Post);
    req->setParameter("tz", zone);
    HttpResponsePtr resp;
    controller.advisorRun(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_TRUE(resp);
    ASSERT_EQ(resp->statusCode(), k202Accepted) << resp->body();
    TestHelpers::expect_matches_schema(resp, "POST", "/api/v1/money/advisor/run");
    const auto week = Money::Period::of(Money::Period::Kind::week, local);
    EXPECT_EQ(body_of(resp)["data"]["period_start"], week.from);
    EXPECT_EQ(body_of(resp)["data"]["period_end"], week.to);
    auto job = Jobs::get().pick({"money_advisor"}, 1);
    ASSERT_TRUE(job.has_value());
    Jobs::get().complete(job->id, json::object());
}

// ── the outbox path ─────────────────────────────────────────────────────────

namespace {

/// With a drain configured the parse and advisor jobs go through the outbox,
/// inside the row's transaction. One fixture: both LLM settings are present.
class MoneyOutboxApiTest : public MoneyApiTest {
protected:
    std::string config_file_name() const override { return "money_outbox_api_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override {
        MoneyApiTest::config_overrides(cfg);
        cfg["jobs"]["enabled"] = true;
        cfg["jobs"]["result_ttl"] = 3600;
        cfg["money"]["llm"]["base_url"] = "https://llm.example/v1";
        cfg["money"]["llm"]["api_key"] = "sk-test";
        cfg["money"]["llm"]["model"] = "gpt-6-luna";
        cfg["money"]["llm"]["prompt_parse"] = "Answer with JSON.";
        cfg["money"]["llm"]["prompt_advisor"] = "Write a review.";
        // An hourly tick never fires during the test: every drain below is an explicit call.
        cfg["outbox"]["drain_interval_sec"] = 3600;
    }

    void SetUp() override {
        MoneyApiTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE money_parse_jobs, money_advisor_reports, outbox");
            return true;
        });
        TestHelpers::drain_jobs({"money_parse", "money_advisor"});
    }

    void TearDown() override {
        if (Database::is_initialized()) {
            try {
                Database::get().execute_write([](auto& txn) {
                    txn.exec("TRUNCATE TABLE outbox");
                    return true;
                });
            } catch (...) {}
        }
        MoneyApiTest::TearDown();
    }

    static json outbox_rows() {
        return Database::get().execute_read_primary([](auto& txn) {
            auto r = txn.exec(
                "SELECT COALESCE(json_agg(json_build_object('kind', kind, 'payload', payload)), '[]') FROM outbox");
            return json::parse(r[0][0].template as<std::string>());
        });
    }

    static long queued(const char* type) {
        const auto depth = Jobs::get().queue_depth_by_type();
        return depth.count(type) ? depth.at(type) : 0;
    }
};

}  // namespace

TEST_F(MoneyOutboxApiTest, WithADrainTheParseJobRidesTheRowsTransaction) {
    ASSERT_TRUE(Jobs::Outbox::drain_configured());
    const auto text = call_json(&Api::MoneyController::parseText, user(kAnna), json{{"text", "Kaspi *8880 100 KZT"}});
    ASSERT_EQ(text->statusCode(), k202Accepted) << text->body();
    const std::string id = body_of(text)["data"]["id"];

    EXPECT_EQ(queued("money_parse"), 0) << "nothing is submitted directly";
    const json rows = outbox_rows();
    ASSERT_EQ(rows.size(), 1u) << rows.dump();
    EXPECT_EQ(rows[0]["kind"], "money_parse");
    EXPECT_EQ(rows[0]["payload"]["job_id"], id);
    EXPECT_EQ(rows[0]["payload"]["owner_id"], kAnna);
    EXPECT_EQ(rows[0]["payload"]["max_attempts"], Jobs::get().default_max_retries());

    EXPECT_EQ(Jobs::Outbox::drain().submitted, 1);
    auto job = Jobs::get().pick({"money_parse"}, 1);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->payload["job_id"], id);
    Jobs::get().complete(job->id, json::object());
}

TEST_F(MoneyOutboxApiTest, WithADrainTheAdvisorRunRidesTheRowsTransaction) {
    ASSERT_TRUE(Jobs::Outbox::drain_configured());
    const auto first =
        call_json(&Api::MoneyController::advisorRun, user(kAnna), json{{"period", "month"}, {"date", "2026-10-07"}});
    ASSERT_EQ(first->statusCode(), k202Accepted) << first->body();
    const json row = body_of(first)["data"];
    EXPECT_EQ(body_of(first)["queued"], true);

    EXPECT_EQ(queued("money_advisor"), 0) << "nothing is submitted directly";
    const json rows = outbox_rows();
    ASSERT_EQ(rows.size(), 1u) << rows.dump();
    EXPECT_EQ(rows[0]["kind"], "money_advisor");
    EXPECT_EQ(rows[0]["payload"]["report_id"], row["id"]);
    EXPECT_EQ(rows[0]["payload"]["owner_id"], kAnna);
    EXPECT_EQ(rows[0]["payload"]["max_attempts"], Jobs::get().default_max_retries());

    // The same month again queues nothing, in the outbox either.
    const auto again =
        call_json(&Api::MoneyController::advisorRun, user(kAnna), json{{"period", "month"}, {"date", "2026-10-20"}});
    EXPECT_EQ(again->statusCode(), k200OK);
    EXPECT_EQ(body_of(again)["queued"], false);
    EXPECT_EQ(outbox_rows().size(), 1u);

    EXPECT_EQ(Jobs::Outbox::drain().submitted, 1);
    auto job = Jobs::get().pick({"money_advisor"}, 1);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->payload["report_id"], row["id"]);
    Jobs::get().complete(job->id, json::object());
}

// A currency's decimals cannot shrink under an account that keeps it: the
// rows hold amounts at the current scale (check_scale_in). More decimals,
// or the same, are fine; another user's accounts do not count.
TEST_F(MoneyApiTest, FewerDecimalsAreRefusedWhileAnAccountKeepsTheCurrency) {
    const auto xau = call_json(&Api::MoneyController::upsertCurrency,
                               user(kAnna),
                               json{{"code", "XAU"}, {"decimals", 3}, {"minor_unit", "milli-ounce"}});
    ASSERT_EQ(xau->statusCode(), k200OK) << xau->body();
    const auto patch = [&](const char* who, const json& body) {
        return call_id(&Api::MoneyController::patchCurrency, user(who), Patch, "XAU", body);
    };
    EXPECT_EQ(body_of(patch(kAnna, json{{"decimals", 2}}))["data"]["decimals"], 2) << "no account yet: any change";
    account(kAnna, "Gold", "XAU");

    const auto refused = patch(kAnna, json{{"decimals", 1}});
    ASSERT_EQ(refused->statusCode(), k409Conflict) << refused->body();
    EXPECT_EQ(body_of(refused)["error"], "decimals_in_use");
    int kept = -1;
    // Bound to a local first: a range-for over `temporary["data"]` reads the
    // temporary after it is gone (the loop keeps only the subobject alive).
    const json listed = body_of(call(&Api::MoneyController::listCurrencies, user(kAnna), Get));
    for (const auto& c : listed["data"]) {
        if (c["code"] == "XAU") {
            kept = c["decimals"].get<int>();
        }
    }
    EXPECT_EQ(kept, 2) << "nothing changed";

    EXPECT_EQ(body_of(patch(kAnna, json{{"decimals", 2}}))["data"]["decimals"], 2) << "the same is not a change";
    EXPECT_EQ(body_of(patch(kAnna, json{{"decimals", 4}}))["data"]["decimals"], 4) << "more decimals fit every row";
    EXPECT_EQ(body_of(patch(kAnna, json{{"name", "Gold"}}))["data"]["name"], "Gold") << "a patch without decimals";

    // POST on the existing code updates it: the same rule holds there.
    const auto upsert = [&](const json& body) {
        return call_json(&Api::MoneyController::upsertCurrency, user(kAnna), body);
    };
    const auto shrunk = upsert(json{{"code", "XAU"}, {"decimals", 1}, {"minor_unit", "deci-ounce"}});
    ASSERT_EQ(shrunk->statusCode(), k409Conflict) << shrunk->body();
    EXPECT_EQ(body_of(shrunk)["error"], "decimals_in_use");
    EXPECT_EQ(body_of(patch(kAnna, json{{"name", "Gold"}}))["data"]["decimals"], 4) << "the upsert changed nothing";
    const auto wider = upsert(json{{"code", "XAU"}, {"decimals", 5}, {"minor_unit", "ounce/100000"}});
    ASSERT_EQ(wider->statusCode(), k200OK) << wider->body();
    EXPECT_EQ(body_of(wider)["data"]["decimals"], 5);

    // Boris keeps XAU too, without an account in it: his decimals are his.
    ASSERT_EQ(call_json(&Api::MoneyController::upsertCurrency,
                        user(kBoris),
                        json{{"code", "XAU"}, {"decimals", 3}, {"minor_unit", "milli-ounce"}})
                  ->statusCode(),
              k200OK);
    EXPECT_EQ(body_of(patch(kBoris, json{{"decimals", 1}}))["data"]["decimals"], 1);
}
