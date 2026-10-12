/**
 * @file test_money_summary_api.cpp
 * @brief GET /api/v1/money/summary: all accounts in one currency at the day's
 *        rates, an archived account left out, a currency without a rate listed
 *        apart (partial), a transfer between own accounts, the day series, the
 *        change split adding up, bad queries.
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
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kOwner = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaab9";
constexpr const char* kDay = "2026-10-13";

Security::Auth::AuthPrincipal owner() {
    Security::Auth::AuthPrincipal p;
    p.subject = kOwner;
    p.raw_claims = json{{"sub", kOwner}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

json body_of(const HttpResponsePtr& resp) {
    return json::parse(std::string(resp->body()));
}

class MoneySummaryApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::MoneyController c;

    std::string config_file_name() const override { return "money_summary_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["money"]["enabled"] = true; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            const std::string o = kOwner;
            txn.exec_params("DELETE FROM money_transfers WHERE owner_id = $1::uuid", o);
            txn.exec_params("DELETE FROM money_transactions WHERE owner_id = $1::uuid", o);
            txn.exec_params("DELETE FROM money_accounts WHERE owner_id = $1::uuid", o);
            txn.exec_params("DELETE FROM money_categories WHERE owner_id = $1::uuid", o);
            txn.exec_params("DELETE FROM money_currencies WHERE owner_id = $1::uuid", o);
            txn.exec("DELETE FROM money_fx_rates WHERE quote IN ('KZT', 'EUR', 'THB')");
            txn.exec_params(
                "INSERT INTO money_currencies (owner_id, code) VALUES ($1::uuid, 'KZT'), ($1::uuid, 'USD'), "
                "($1::uuid, 'THB')",
                o);
            txn.exec_params(
                "INSERT INTO money_accounts (id, owner_id, name, currency, opening_balance) VALUES "
                "('c0000000-0000-4000-8000-000000000001', $1::uuid, 'Kaspi', 'KZT', 100000), "
                "('c0000000-0000-4000-8000-000000000002', $1::uuid, 'Wise', 'USD', 50), "
                "('c0000000-0000-4000-8000-000000000003', $1::uuid, 'Old', 'USD', 999)",
                o);
            txn.exec_params(
                "UPDATE money_accounts SET archived = true WHERE id = 'c0000000-0000-4000-8000-000000000003'", o);
            txn.exec(
                "INSERT INTO money_fx_rates (date, quote, per_usd) VALUES "
                "('2026-10-01', 'KZT', 500), ('2026-10-10', 'KZT', 490), ('2026-10-01', 'EUR', 0.9)");
            return true;
        });
    }

    HttpResponsePtr summary(const std::vector<std::pair<std::string, std::string>>& params) {
        auto req = TestHelpers::authed_json(owner(), json::object(), Get);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        c.summary(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    json ok(const std::vector<std::pair<std::string, std::string>>& params) {
        auto r = summary(params);
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        return body_of(r)["data"];
    }
};

}  // namespace

TEST_F(MoneySummaryApiTest, AllAccountsInUsdLeaveTheArchivedOut) {
    const json d = ok({{"currency", "USD"}, {"days", "30"}, {"date", kDay}});
    EXPECT_NEAR(d["total"].get<double>(), 100000.0 / 490.0 + 50.0, 1e-6);
    EXPECT_FALSE(d["partial"].get<bool>());
    ASSERT_EQ(d["accounts"].size(), 2u) << "the archived account is out";
    EXPECT_EQ(d["accounts"][0]["name"], "Kaspi") << "largest value first";
    EXPECT_NEAR(d["accounts"][0]["rate"].get<double>(), 490.0, 1e-9);
    EXPECT_EQ(d["accounts"][0]["rate_date"], "2026-10-10");
    EXPECT_EQ(d["rate_date"], "2026-10-10");
    EXPECT_EQ(d["days"].size(), 30u);
    EXPECT_EQ(d["days"].back()["date"], kDay);
}

TEST_F(MoneySummaryApiTest, InEurThroughTheDollar) {
    const json d = ok({{"currency", "EUR"}, {"days", "30"}, {"date", kDay}});
    EXPECT_NEAR(d["total"].get<double>(), (100000.0 / 490.0 + 50.0) * 0.9, 1e-6);
}

TEST_F(MoneySummaryApiTest, ACurrencyWithoutARateIsListedApart) {
    Database::get().execute_write([](auto& txn) {
        txn.exec_params(
            "INSERT INTO money_accounts (owner_id, name, currency, opening_balance) "
            "VALUES ($1::uuid, 'Bangkok', 'THB', 3000)",
            std::string(kOwner));
        return true;
    });
    const json d = ok({{"currency", "USD"}, {"days", "30"}, {"date", kDay}});
    EXPECT_TRUE(d["partial"].get<bool>());
    ASSERT_EQ(d["not_converted"].size(), 1u);
    EXPECT_EQ(d["not_converted"][0]["currency"], "THB");
    EXPECT_DOUBLE_EQ(d["not_converted"][0]["amount"].get<double>(), 3000.0);
    EXPECT_NEAR(d["total"].get<double>(), 100000.0 / 490.0 + 50.0, 1e-6) << "THB is not counted as anything";
}

TEST_F(MoneySummaryApiTest, ATransferMovesMoneyWithoutCreatingIt) {
    Database::get().execute_write([](auto& txn) {
        txn.exec_params(
            "INSERT INTO money_transfers (owner_id, date, from_account_id, to_account_id, amount_sent, "
            "amount_received, fee) VALUES ($1::uuid, '2026-10-12', 'c0000000-0000-4000-8000-000000000002', "
            "'c0000000-0000-4000-8000-000000000001', 10, 4900, 1)",
            std::string(kOwner));
        return true;
    });
    const json d = ok({{"currency", "USD"}, {"days", "30"}, {"date", kDay}});
    EXPECT_NEAR(d["total"].get<double>(), (100000.0 + 4900.0) / 490.0 + 50.0 - 11.0, 1e-6)
        << "10 USD went over as 4900 KZT (10 USD at 490) and 1 USD of fee is gone";
}

TEST_F(MoneySummaryApiTest, TheChangeSplitAddsUp) {
    const json d = ok({{"currency", "USD"}, {"days", "30"}, {"date", kDay}});
    const json ch = d["change"];
    EXPECT_NEAR(ch["ledger"].get<double>() + ch["rates"].get<double>(), ch["total"].get<double>(), 1e-6);
    EXPECT_NEAR(ch["total"].get<double>(),
                d["days"].back()["total"].get<double>() - d["days"].front()["total"].get<double>(),
                1e-6);
}

TEST_F(MoneySummaryApiTest, DaysBeforeTheFirstRateArePartial) {
    const json d = ok({{"currency", "USD"}, {"days", "90"}, {"date", kDay}});
    EXPECT_EQ(d["days"].size(), 90u);
    EXPECT_TRUE(d["days"].front()["partial"].get<bool>()) << "KZT had no quote yet";
    EXPECT_FALSE(d["days"].back()["partial"].get<bool>());
}

TEST_F(MoneySummaryApiTest, BadQueries) {
    EXPECT_EQ(summary({{"days", "31"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(summary({{"currency", "usd"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(summary({{"date", "2026-02-30"}})->statusCode(), k400BadRequest);
    auto r = summary({{"currency", "JPY"}, {"date", kDay}});
    EXPECT_EQ(r->statusCode(), k422UnprocessableEntity);
    EXPECT_EQ(body_of(r)["error"], "no_rate");
    EXPECT_EQ(summary({})->statusCode(), k200OK) << "USD, 90 days, today by default";
}
