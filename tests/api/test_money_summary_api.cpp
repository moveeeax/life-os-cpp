/**
 * @file test_money_summary_api.cpp
 * @brief GET /api/v1/money/summary: all accounts in one currency at the day's
 *        rates, an archived account left out, a currency without a rate listed
 *        apart (partial), a transfer between own accounts, the day series, the
 *        change split adding up (partial without a start rate), account rows at
 *        the date, bad queries.
 */

#include <string>
#include <tuple>
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
// Another owner with money of his own in every test: a summary that dropped
// its owner filter would add his accounts to every total below.
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb9";
constexpr const char* kDay = "2026-10-13";

Security::Auth::AuthPrincipal owner() {
    Security::Auth::AuthPrincipal p;
    p.subject = kOwner;
    p.raw_claims = json{{"sub", kOwner}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

using TestHelpers::body_of;

class MoneySummaryApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::MoneyController c;

    std::string config_file_name() const override { return "money_summary_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["money"]["enabled"] = true; }

    // Quotes are shared by every owner: the suite needs KZT, EUR and THB to hold
    // only its own rows, so it keeps the rows it found and puts them back after.
    std::vector<std::vector<std::string>> saved_quotes_;

    void TearDown() override {
        if (!::testing::Test::IsSkipped()) {
            Database::get().execute_write([&](auto& txn) {
                txn.exec("DELETE FROM money_fx_rates WHERE quote IN ('KZT', 'EUR', 'THB')");
                for (const auto& q : saved_quotes_) {
                    txn.exec_params(
                        "INSERT INTO money_fx_rates (date, quote, per_usd) VALUES ($1::date, $2, $3::numeric)",
                        q[0],
                        q[1],
                        q[2]);
                }
                return true;
            });
        }
        TestHelpers::CoreBackedTest::TearDown();
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        saved_quotes_ = Database::get().execute_read([](auto& txn) {
            std::vector<std::vector<std::string>> rows;
            for (const auto& r : txn.exec("SELECT date::text, quote, per_usd::text FROM money_fx_rates WHERE quote IN "
                                          "('KZT', 'EUR', 'THB')")) {
                rows.push_back({r[0].template as<std::string>(),
                                r[1].template as<std::string>(),
                                r[2].template as<std::string>()});
            }
            return rows;
        });
        Database::get().execute_write([](auto& txn) {
            const std::string o = kOwner;
            for (const std::string& who : {std::string(kOwner), std::string(kBoris)}) {
                txn.exec_params("DELETE FROM money_transfers WHERE owner_id = $1::uuid", who);
                txn.exec_params("DELETE FROM money_transactions WHERE owner_id = $1::uuid", who);
                txn.exec_params("DELETE FROM money_accounts WHERE owner_id = $1::uuid", who);
                txn.exec_params("DELETE FROM money_categories WHERE owner_id = $1::uuid", who);
                txn.exec_params("DELETE FROM money_currencies WHERE owner_id = $1::uuid", who);
            }
            const std::string b = kBoris;
            txn.exec_params("INSERT INTO money_currencies (owner_id, code) VALUES ($1::uuid, 'KZT'), ($1::uuid, 'USD')",
                            b);
            txn.exec_params(
                "INSERT INTO money_accounts (id, owner_id, name, currency, opening_balance) VALUES "
                "('c0000000-0000-4000-8000-0000000000b1', $1::uuid, 'Boris Kaspi', 'KZT', 9000000), "
                "('c0000000-0000-4000-8000-0000000000b2', $1::uuid, 'Boris Wise', 'USD', 7777)",
                b);
            txn.exec_params(
                "INSERT INTO money_transactions (owner_id, type, date, account_id, amount, name) VALUES "
                "($1::uuid, 'income', '2026-10-05', 'c0000000-0000-4000-8000-0000000000b2', 1000, 'Boris pay')",
                b);
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
            txn.exec("UPDATE money_accounts SET archived = true WHERE id = 'c0000000-0000-4000-8000-000000000003'");
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
        TestHelpers::expect_2xx_matches_schema(captured, "GET", "/api/v1/money/summary");
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

TEST_F(MoneySummaryApiTest, TheChangeWithoutAStartRateIsPartial) {
    // From is 09-14 and KZT has no quote before 10-01: the split covers USD alone.
    const json d = ok({{"currency", "USD"}, {"days", "30"}, {"date", kDay}});
    const json ch = d["change"];
    EXPECT_TRUE(ch["partial"].get<bool>());
    EXPECT_NEAR(ch["ledger"].get<double>() + ch["rates"].get<double>(), ch["total"].get<double>(), 1e-6);
}

TEST_F(MoneySummaryApiTest, TheChangeSplitAddsUpToTheLine) {
    Database::get().execute_write([](auto& txn) {
        txn.exec("INSERT INTO money_fx_rates (date, quote, per_usd) VALUES ('2026-09-01', 'KZT', 520)");
        txn.exec_params(
            "INSERT INTO money_transfers (owner_id, date, from_account_id, to_account_id, amount_sent, "
            "amount_received, fee) VALUES ($1::uuid, '2026-10-12', 'c0000000-0000-4000-8000-000000000002', "
            "'c0000000-0000-4000-8000-000000000001', 10, 4900, 1)",
            std::string(kOwner));
        return true;
    });
    const json d = ok({{"currency", "USD"}, {"days", "30"}, {"date", kDay}});
    const json ch = d["change"];
    EXPECT_FALSE(ch["partial"].get<bool>());
    EXPECT_NEAR(ch["ledger"].get<double>() + ch["rates"].get<double>(), ch["total"].get<double>(), 1e-6);
    EXPECT_NEAR(ch["total"].get<double>(),
                d["days"].back()["total"].get<double>() - d["days"].front()["total"].get<double>(),
                1e-6);
    EXPECT_NEAR(ch["rates"].get<double>(), 100000.0 / 490.0 - 100000.0 / 520.0, 1e-6) << "KZT got dearer";
}

TEST_F(MoneySummaryApiTest, AccountRowsAreAtTheDate) {
    Database::get().execute_write([](auto& txn) {
        txn.exec_params(
            "WITH c AS (INSERT INTO money_categories (owner_id, name, kind) VALUES ($1::uuid, 'Food', 'expense') "
            " RETURNING id) "
            "INSERT INTO money_transactions (owner_id, type, date, account_id, category_id, amount, name) "
            "SELECT $1::uuid, 'expense', '2026-10-20', 'c0000000-0000-4000-8000-000000000001', c.id, 9000, 'Later' "
            "FROM c",
            std::string(kOwner));
        return true;
    });
    const json d = ok({{"currency", "USD"}, {"days", "30"}, {"date", kDay}});
    ASSERT_EQ(d["accounts"][0]["name"], "Kaspi");
    EXPECT_DOUBLE_EQ(d["accounts"][0]["balance"].get<double>(), 100000.0) << "a spend after the date is not in";
    EXPECT_NEAR(d["accounts"][0]["value"].get<double>() + d["accounts"][1]["value"].get<double>(),
                d["total"].get<double>(),
                1e-6);
}

TEST_F(MoneySummaryApiTest, AnEmptyAccountWithoutARateLeavesTheTotalWhole) {
    Database::get().execute_write([](auto& txn) {
        txn.exec_params(
            "INSERT INTO money_accounts (owner_id, name, currency, opening_balance) "
            "VALUES ($1::uuid, 'Bangkok', 'THB', 0)",
            std::string(kOwner));
        return true;
    });
    const json d = ok({{"currency", "USD"}, {"days", "30"}, {"date", kDay}});
    EXPECT_FALSE(d["partial"].get<bool>());
    EXPECT_TRUE(d["not_converted"].empty());
}

TEST_F(MoneySummaryApiTest, DaysBeforeTheFirstRateArePartial) {
    const json d = ok({{"currency", "USD"}, {"days", "90"}, {"date", kDay}});
    EXPECT_EQ(d["days"].size(), 90u);
    EXPECT_TRUE(d["days"].front()["partial"].get<bool>()) << "KZT had no quote yet";
    EXPECT_FALSE(d["days"].back()["partial"].get<bool>());
}

TEST_F(MoneySummaryApiTest, WithoutADateTheSummaryEndsOnTodayInTheGivenZone) {
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
    EXPECT_EQ(ok({{"tz", zone}})["date"], local);
    EXPECT_EQ(ok({})["date"], utc) << "no tz: UTC";
    EXPECT_EQ(ok({{"tz", zone}, {"date", kDay}})["date"], kDay) << "a given date wins";
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

// Pending rows (the inbox) are not money yet: neither the balances nor the
// summary count them until they are confirmed.
TEST_F(MoneySummaryApiTest, APendingRowMovesNothingUntilConfirmed) {
    const auto total = [&] { return ok({{"currency", "USD"}, {"days", "30"}, {"date", kDay}})["total"].get<double>(); };
    const auto kaspi_balance = [&] {
        auto req = TestHelpers::authed_json(owner(), json::object(), Get);
        HttpResponsePtr captured;
        c.balances(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        for (const auto& group : body_of(captured)["data"]) {
            if (group["currency"] == "KZT") {
                return group["total"].get<double>();
            }
        }
        return -1.0;
    };
    const double total_before = total();
    const double kaspi_before = kaspi_balance();
    EXPECT_NEAR(kaspi_before, 100000.0, 1e-6);

    HttpResponsePtr created;
    c.createTransaction(TestHelpers::authed_json(owner(),
                                                 json{{"date", "2026-10-12"},
                                                      {"account_id", "c0000000-0000-4000-8000-000000000001"},
                                                      {"amount", 4900},
                                                      {"name", "Dinner"},
                                                      {"status", "pending"}}),
                        [&](const HttpResponsePtr& r) { created = r; });
    ASSERT_TRUE(created);
    ASSERT_EQ(created->statusCode(), k201Created) << std::string(created->body());
    const std::string id = body_of(created)["data"]["id"].get<std::string>();
    EXPECT_NEAR(kaspi_balance(), kaspi_before, 1e-6) << "a pending row is not in the balance";
    EXPECT_NEAR(total(), total_before, 1e-6) << "nor in the summary";

    HttpResponsePtr confirmed;
    c.confirmTransaction(
        TestHelpers::authed_json(owner(), json::object(), Post), [&](const HttpResponsePtr& r) { confirmed = r; }, id);
    ASSERT_TRUE(confirmed);
    ASSERT_EQ(confirmed->statusCode(), k200OK) << std::string(confirmed->body());
    EXPECT_NEAR(kaspi_balance(), kaspi_before - 4900.0, 1e-6) << "confirmed: the expense counts";
    EXPECT_NEAR(total(), total_before - 4900.0 / 490.0, 1e-6);
}
