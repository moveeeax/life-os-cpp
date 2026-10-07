/**
 * @file test_money_repositories.cpp
 * @brief The money repositories against a real database: the seven
 *        invariants as Invariant errors, balances, transfers, the inbox and
 *        the merchant memory, rates, settings, and the owner scope of every
 *        read and write.
 */

#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/money/AccountRepository.hpp"
#include "repositories/money/CategoryRepository.hpp"
#include "repositories/money/CurrencyRepository.hpp"
#include "repositories/money/Errors.hpp"
#include "repositories/money/FxRateRepository.hpp"
#include "repositories/money/MerchantRepository.hpp"
#include "repositories/money/SettingsRepository.hpp"
#include "repositories/money/TransactionRepository.hpp"
#include "repositories/money/TransferRepository.hpp"
#include "test_helpers.hpp"

namespace {

using json = nlohmann::json;
using namespace Repositories::Money;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa5";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb5";

class MoneyRepositoriesTest : public TestHelpers::CoreBackedTest {
protected:
    CurrencyRepository currencies;
    AccountRepository accounts;
    CategoryRepository categories;
    TransactionRepository transactions;
    TransferRepository transfers;
    MerchantRepository merchants;
    FxRateRepository rates;
    SettingsRepository settings;

    json kaspi, cash, freedom, boris_account, food, salary, rent, boris_food;

    std::string config_file_name() const override { return "money_repositories_test_config.json"; }

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
        currencies.seed_defaults(kAnna);
        currencies.seed_defaults(kBoris);
        AccountRepository::Input ka;
        ka.name = "Kaspi Gold Visa •8880";
        ka.bank = "Kaspi";
        ka.currency = "KZT";
        ka.last4 = "8880";
        ka.opening_balance = 1000;
        kaspi = accounts.create(kAnna, ka);
        boris_account = accounts.create(kBoris, ka);
        AccountRepository::Input th;
        th.name = "Cash THB";
        th.kind = "cash";
        th.currency = "THB";
        cash = accounts.create(kAnna, th);
        ka.name = "Freedom KZT •8977";
        ka.last4 = "8977";
        ka.opening_balance = 0;
        freedom = accounts.create(kAnna, ka);
        CategoryRepository::Input c;
        c.name = "Food";
        c.budget_max = 1000;
        c.budget_currency = "USD";
        food = categories.create(kAnna, c);
        boris_food = categories.create(kBoris, c);
        c = {};
        c.name = "Salary";
        c.kind = "income";
        salary = categories.create(kAnna, c);
        c = {};
        c.name = "Rent";
        c.flexibility = "fixed";
        rent = categories.create(kAnna, c);
    }

    /// Groceries on Kaspi, 13275.61 KZT with the receipt in THB (the owner's live row).
    TransactionRepository::Input groceries() const {
        TransactionRepository::Input e;
        e.date = "2026-10-05";
        e.account_id = kaspi["id"];
        e.amount = 13275.61;
        e.category_id = food["id"];
        e.merchant = "Big C Phuket";
        e.name = "Groceries (store, THB)";
        e.receipt_amount = 955.75;
        e.receipt_currency = "THB";
        e.fx_note = "THB -> USD -> KZT";
        return e;
    }

    TransactionRepository::Input income() const {
        TransactionRepository::Input i;
        i.type = "income";
        i.date = "2026-10-01";
        i.account_id = kaspi["id"];
        i.amount = 500000;
        i.category_id = salary["id"];
        i.name = "Salary";
        return i;
    }

    TransactionRepository::Input adjustment(const std::string& of, double amount) const {
        TransactionRepository::Input a;
        a.type = "fx_adjustment";
        a.date = "2026-10-07";
        a.account_id = kaspi["id"];
        a.amount = amount;
        a.adjusts_id = of;
        a.name = "Bank recalculation";
        return a;
    }

    double balance(const std::string& owner, const json& account) {
        return accounts.find(owner, account["id"])->at("balance").get<double>();
    }
};

}  // namespace

TEST_F(MoneyRepositoriesTest, SeedsCurrenciesOncePerUserAndScopesThem) {
    currencies.seed_defaults(kAnna);
    const json list = currencies.list(kAnna, false);
    ASSERT_EQ(list.size(), 10u);
    EXPECT_EQ(list[0]["role"], "primary");
    EXPECT_EQ(list[9]["code"], "SGD") << "no role sorts last";
    EXPECT_THROW(currencies.upsert(kAnna, "kzt", {}), Invariant);
    currencies.upsert(kAnna, "JPY", {"Yen", std::nullopt, 0, false, "yen"});
    EXPECT_EQ(currencies.list(kAnna, false).size(), 11u);
    EXPECT_EQ(currencies.list(kBoris, false).size(), 10u);
    CurrencyRepository::Patch hide;
    hide.archived = true;
    currencies.patch(kAnna, "JPY", hide);
    EXPECT_EQ(currencies.list(kAnna, false).size(), 10u);
    EXPECT_EQ(currencies.list(kAnna, true).size(), 11u);
    EXPECT_THROW(currencies.patch(kAnna, "XXX", hide), NotFound);
}

TEST_F(MoneyRepositoriesTest, AccountsBelongToTheirOwnerAndStartAtTheOpeningBalance) {
    EXPECT_EQ(accounts.list(kAnna, false).size(), 3u);
    EXPECT_EQ(accounts.list(kBoris, false).size(), 1u);
    EXPECT_EQ(kaspi["balance"], 1000);
    EXPECT_TRUE(kaspi["last_activity"].is_null());
    AccountRepository::Input bad;
    bad.name = "x";
    bad.currency = "XXX";
    EXPECT_THROW(accounts.create(kAnna, bad), Invariant);
    EXPECT_FALSE(accounts.find(kBoris, kaspi["id"]).has_value());
    AccountRepository::Patch p;
    p.name = "Renamed";
    EXPECT_THROW(accounts.update(kBoris, kaspi["id"], p), NotFound);
    EXPECT_EQ(accounts.update(kAnna, kaspi["id"], p)["name"], "Renamed");
}

TEST_F(MoneyRepositoriesTest, CategoriesCarryOneBudgetInOneCurrency) {
    CategoryRepository::Input half;
    half.name = "Half";
    half.budget_max = 10;
    EXPECT_THROW(categories.create(kAnna, half), Invariant) << "a budget without its currency";
    half.budget_currency = "XXX";
    EXPECT_THROW(categories.create(kAnna, half), Invariant) << "a currency that is not the owner's";
    CategoryRepository::Patch p;
    p.budget_max = std::optional<double>();
    p.budget_currency = std::optional<std::string>();
    EXPECT_TRUE(categories.update(kAnna, food["id"], p)["budget_max"].is_null());
    EXPECT_THROW(categories.update(kBoris, food["id"], p), NotFound);
}

TEST_F(MoneyRepositoriesTest, TransactionsCheckTheAccountTheCategoryAndTheAmount) {
    const json t1 = transactions.create(kAnna, groceries());
    EXPECT_EQ(t1["currency"], "KZT");
    EXPECT_EQ(t1["final_amount"], 13275.61);
    EXPECT_EQ(t1["merchant_key"], "big c phuket");
    EXPECT_TRUE(t1["adjustments"].empty());

    auto wrong_kind = groceries();
    wrong_kind.category_id = salary["id"];
    EXPECT_THROW(transactions.create(kAnna, wrong_kind), Invariant);
    auto foreign_account = groceries();
    foreign_account.account_id = boris_account["id"];
    EXPECT_THROW(transactions.create(kAnna, foreign_account), Invariant);
    auto foreign_category = groceries();
    foreign_category.category_id = boris_food["id"];
    EXPECT_THROW(transactions.create(kAnna, foreign_category), Invariant);
    auto no_category = groceries();
    no_category.category_id.reset();
    EXPECT_THROW(transactions.create(kAnna, no_category), Invariant);
    auto negative = groceries();
    negative.amount = -5;
    EXPECT_THROW(transactions.create(kAnna, negative), Invariant);
    auto bad_date = groceries();
    bad_date.date = "2026-02-30";
    EXPECT_THROW(transactions.create(kAnna, bad_date), InvalidDate);
    EXPECT_FALSE(transactions.find(kBoris, t1["id"]).has_value());
}

TEST_F(MoneyRepositoriesTest, AdjustmentsPointAtTheirOriginalOnTheSameAccountAndMoveTheBalance) {
    const json t1 = transactions.create(kAnna, groceries());
    transactions.create(kAnna, income());
    const json a1 = transactions.create(kAnna, adjustment(t1["id"], 120.5));
    EXPECT_TRUE(a1["category_id"].is_null());
    transactions.create(kAnna, adjustment(t1["id"], -20));
    const json t1_now = *transactions.find(kAnna, t1["id"]);
    EXPECT_DOUBLE_EQ(t1_now["final_amount"].get<double>(), 13376.11);
    EXPECT_EQ(t1_now["adjustments"].size(), 2u);

    auto other_account = adjustment(t1["id"], 1);
    other_account.account_id = freedom["id"];
    EXPECT_THROW(transactions.create(kAnna, other_account), Invariant);
    EXPECT_THROW(transactions.create(kAnna, adjustment(a1["id"], 1)), Invariant) << "no adjustment of an adjustment";
    auto foreign = adjustment(t1["id"], 1);
    foreign.account_id = boris_account["id"];
    EXPECT_THROW(transactions.create(kBoris, foreign), Invariant) << "another user's original";
    EXPECT_THROW(transactions.create(kAnna, adjustment(t1["id"], 0)), Invariant);
    auto not_adjustment = groceries();
    not_adjustment.adjusts_id = t1["id"];
    EXPECT_THROW(transactions.create(kAnna, not_adjustment), Invariant) << "only an adjustment points at a row";

    // 1000 + 500000 − 13275.61 − 120.5 + 20
    EXPECT_DOUBLE_EQ(balance(kAnna, kaspi), 487623.89);
    EXPECT_EQ(accounts.find(kAnna, kaspi["id"])->at("last_activity"), "2026-10-07");
}

TEST_F(MoneyRepositoriesTest, TransfersNeedTwoOwnAccountsAndTheReceivedAmountOnlyAcrossCurrencies) {
    TransferRepository::Input same;
    same.date = "2026-10-06";
    same.from_account_id = kaspi["id"];
    same.to_account_id = freedom["id"];
    same.amount_sent = 1000;
    same.fee = 10;
    const json x1 = transfers.create(kAnna, same);
    EXPECT_TRUE(x1["amount_received"].is_null());
    EXPECT_TRUE(x1["cost_rate"].is_null());
    auto same_received = same;
    same_received.amount_received = 999;
    EXPECT_THROW(transfers.create(kAnna, same_received), Invariant);

    TransferRepository::Input cross;
    cross.date = "2026-10-06";
    cross.from_account_id = kaspi["id"];
    cross.to_account_id = cash["id"];
    cross.amount_sent = 13000;
    EXPECT_THROW(transfers.create(kAnna, cross), Invariant) << "different currencies need the amount received";
    cross.amount_received = 1000;
    const json x2 = transfers.create(kAnna, cross);
    EXPECT_EQ(x2["cost_rate"], 13);
    EXPECT_EQ(x2["from_currency"], "KZT");
    EXPECT_EQ(x2["to_currency"], "THB");
    auto self = same;
    self.to_account_id = kaspi["id"];
    EXPECT_THROW(transfers.create(kAnna, self), Invariant);
    auto to_boris = same;
    to_boris.to_account_id = boris_account["id"];
    EXPECT_THROW(transfers.create(kAnna, to_boris), Invariant);

    // kaspi 1000 − 1010 − 13000; freedom +1000 (sent, received null); cash +1000
    EXPECT_DOUBLE_EQ(balance(kAnna, kaspi), -13010);
    EXPECT_DOUBLE_EQ(balance(kAnna, freedom), 1000);
    EXPECT_DOUBLE_EQ(balance(kAnna, cash), 1000);

    TransferRepository::Patch p;
    p.amount_received = std::optional<double>(1100);
    EXPECT_EQ(transfers.update(kAnna, x2["id"], p)["amount_received"], 1100);
    p.amount_received = std::optional<double>();
    EXPECT_THROW(transfers.update(kAnna, x2["id"], p), Invariant);
    EXPECT_EQ(transfers.list(kAnna, {}).size(), 2u);
    TransferRepository::Filter f;
    f.account_id = cash["id"];
    EXPECT_EQ(transfers.list(kAnna, f).size(), 1u);
    EXPECT_EQ(transfers.list(kBoris, {}).size(), 0u);
    transfers.remove(kAnna, x1["id"]);
    EXPECT_THROW(transfers.remove(kBoris, x2["id"]), NotFound);
}

TEST_F(MoneyRepositoriesTest, ListFiltersPagesAndEscapesTheSearch) {
    const json t1 = transactions.create(kAnna, groceries());
    transactions.create(kAnna, income());
    transactions.create(kAnna, adjustment(t1["id"], 1));
    auto thb = groceries();
    thb.account_id = cash["id"];
    thb.amount = 955.75;
    thb.receipt_amount.reset();
    thb.receipt_currency.reset();
    thb.fx_note.clear();
    transactions.create(kAnna, thb);

    TransactionRepository::Filter f;
    EXPECT_EQ(transactions.list(kAnna, f).total, 4);
    f.currency = "KZT";
    EXPECT_EQ(transactions.list(kAnna, f).total, 3);
    f = {};
    f.type = "expense";
    EXPECT_EQ(transactions.list(kAnna, f).total, 2);
    f = {};
    f.q = "big c";
    EXPECT_EQ(transactions.list(kAnna, f).total, 2);
    f.q = "%";
    EXPECT_EQ(transactions.list(kAnna, f).total, 0) << "% is text";
    f = {};
    f.from = "2026-10-05";
    f.to = "2026-10-05";
    EXPECT_EQ(transactions.list(kAnna, f).total, 2);
    f = {};
    f.limit = 2;
    EXPECT_EQ(transactions.list(kAnna, f).rows.size(), 2u);
    EXPECT_EQ(transactions.list(kAnna, f).total, 4);
    EXPECT_EQ(transactions.list(kAnna, f).rows[0]["date"], "2026-10-07") << "newest first";
    f = {};
    EXPECT_EQ(transactions.list(kBoris, f).total, 0);
}

TEST_F(MoneyRepositoriesTest, BatchWritesAllOrNothing) {
    auto ok = groceries();
    auto negative = groceries();
    negative.amount = -1;
    EXPECT_THROW(transactions.create_many(kAnna, {ok, negative}), Invariant);
    EXPECT_EQ(transactions.list(kAnna, {}).total, 0);
    EXPECT_EQ(transactions.create_many(kAnna, {ok, ok}).size(), 2u);
    EXPECT_EQ(transactions.list(kAnna, {}).total, 2);
}

TEST_F(MoneyRepositoriesTest, InboxFlagsDuplicatesAndConfirmTeachesTheMerchant) {
    transactions.create(kAnna, groceries());
    auto pending = groceries();
    pending.status = "pending";
    pending.source = "text";
    const json p1 = transactions.create(kAnna, pending);
    json inbox = transactions.inbox(kAnna);
    ASSERT_EQ(inbox.size(), 1u);
    EXPECT_EQ(inbox[0]["possible_duplicate"], true);
    pending.amount = 1;
    pending.merchant = "New Place";
    const json p2 = transactions.create(kAnna, pending);
    inbox = transactions.inbox(kAnna);
    ASSERT_EQ(inbox.size(), 2u);
    EXPECT_TRUE(merchants.suggest(kAnna, "new", 10).empty()) << "a pending row teaches nothing yet";

    transactions.confirm(kAnna, p2["id"]);
    EXPECT_EQ(transactions.inbox(kAnna).size(), 1u);
    const json learned = merchants.suggest(kAnna, "new", 10);
    ASSERT_EQ(learned.size(), 1u);
    EXPECT_EQ(learned[0]["category_id"], food["id"]);
    EXPECT_EQ(merchants.suggest(kAnna, "big", 10)[0]["times"], 1);
    EXPECT_THROW(transactions.confirm(kBoris, p1["id"]), NotFound);
    EXPECT_THROW(transactions.confirm(kAnna, p2["id"]), NotFound) << "a posted row is not confirmed twice";
    EXPECT_EQ(merchants.suggest(kAnna, "new", 10)[0]["times"], 1);
    EXPECT_TRUE(transactions.inbox(kBoris).empty());

    merchants.set_category(kAnna, "new place", rent["id"]);
    EXPECT_EQ(merchants.suggest(kAnna, "new", 10)[0]["category_id"], rent["id"]);
    EXPECT_THROW(merchants.set_category(kBoris, "new place", std::nullopt), NotFound);
    EXPECT_THROW(merchants.set_category(kAnna, "new place", boris_food["id"].get<std::string>()), Invariant)
        << "another user's category is not a category here";
    EXPECT_EQ(merchants.top(kAnna, 10).size(), 2u);
}

TEST_F(MoneyRepositoriesTest, UpdateKeepsTheRules) {
    const json t1 = transactions.create(kAnna, groceries());
    const json a1 = transactions.create(kAnna, adjustment(t1["id"], 120.5));
    TransactionRepository::Patch p;
    p.note = "x";
    p.amount = 5;
    EXPECT_EQ(transactions.update(kAnna, a1["id"], p)["amount"], 5);
    p = {};
    p.name = "y";
    EXPECT_THROW(transactions.update(kAnna, a1["id"], p), Invariant) << "an adjustment changes amount and note only";
    p = {};
    p.amount = 0;
    EXPECT_THROW(transactions.update(kAnna, a1["id"], p), Invariant);
    p = {};
    p.account_id = freedom["id"];
    EXPECT_THROW(transactions.update(kAnna, t1["id"], p), Invariant) << "a row with adjustments stays put";
    p = {};
    p.category_id = salary["id"];
    EXPECT_THROW(transactions.update(kAnna, t1["id"], p), Invariant);
    p = {};
    p.merchant = "BIG C";
    p.receipt_amount = std::optional<double>();
    p.receipt_currency = std::optional<std::string>();
    const json u = transactions.update(kAnna, t1["id"], p);
    EXPECT_TRUE(u["receipt_amount"].is_null());
    EXPECT_EQ(u["merchant_key"], "big c");
    EXPECT_THROW(transactions.update(kBoris, t1["id"], p), NotFound);
}

TEST_F(MoneyRepositoriesTest, RemoveCascadesAdjustmentsAndArchivesWhatIsInUse) {
    const json t1 = transactions.create(kAnna, groceries());
    const json a1 = transactions.create(kAnna, adjustment(t1["id"], 1));
    const json t2 = transactions.create(kAnna, income());
    EXPECT_THROW(transactions.remove(kBoris, t2["id"]), NotFound);
    transactions.remove(kAnna, t1["id"]);
    EXPECT_FALSE(transactions.find(kAnna, a1["id"]).has_value()) << "the adjustment went with its original";

    EXPECT_EQ(accounts.remove(kAnna, kaspi["id"]), "archived");
    EXPECT_EQ(accounts.remove(kAnna, cash["id"]), "deleted");
    EXPECT_THROW(accounts.remove(kBoris, freedom["id"]), NotFound);
    EXPECT_EQ(categories.remove(kAnna, salary["id"]), "archived");
    EXPECT_EQ(categories.remove(kAnna, rent["id"]), "deleted");
    EXPECT_THROW(categories.remove(kBoris, food["id"]), NotFound);
}

TEST_F(MoneyRepositoriesTest, ReportRowsAndMerchantsBefore) {
    transactions.create(kAnna, groceries());
    transactions.create(kAnna, income());
    const json rows = transactions.report_rows(kAnna, "2026-10-01", "2026-10-31");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_TRUE(rows[0].contains("flexibility"));
    EXPECT_TRUE(rows[0].contains("currency"));
    EXPECT_EQ(transactions.report_rows(kAnna, "2026-09-01", "2026-09-30").size(), 0u);
    EXPECT_EQ(transactions.merchants_before(kAnna, "2026-10-06").size(), 1u);
    EXPECT_EQ(transactions.merchants_before(kAnna, "2026-10-05").size(), 0u);
}

TEST_F(MoneyRepositoriesTest, RatesReadTheNearestEarlierDay) {
    rates.put_day("2026-10-03", {{"KZT", 450}, {"THB", 33.5}});
    rates.put_day("2026-10-03", {{"KZT", 451}});
    EXPECT_DOUBLE_EQ(rates.nearest("2026-10-05", "KZT")->per_usd, 451);
    EXPECT_EQ(rates.nearest("2026-10-05", "KZT")->date, "2026-10-03");
    EXPECT_FALSE(rates.nearest("2026-10-02", "KZT").has_value());
    EXPECT_DOUBLE_EQ(rates.nearest("2026-10-02", "USD")->per_usd, 1);
    EXPECT_TRUE(rates.has_day("2026-10-03"));
    EXPECT_FALSE(rates.has_day("2026-10-04"));
    const auto needed = rates.quotes_needed();
    EXPECT_EQ(needed.count("THB"), 1u);
    EXPECT_EQ(needed.count("USD"), 1u);
}

TEST_F(MoneyRepositoriesTest, SettingsRoundTrip) {
    EXPECT_EQ(settings.load(kAnna)["advisor_enabled"], false);
    SettingsRepository::Input in;
    in.view_currency = "KZT";
    in.advisor_enabled = true;
    in.advisor_weekday = 7;
    in.advisor_currencies = {"KZT", "THB"};
    in.advisor_note = "n";
    const json s = settings.put(kAnna, in);
    EXPECT_EQ(s["advisor_currencies"].size(), 2u);
    EXPECT_EQ(s["view_currency"], "KZT");
    EXPECT_EQ(settings.advisor_owners(7).size(), 1u);
    EXPECT_TRUE(settings.advisor_owners(1).empty());
    in.advisor_weekday = 9;
    EXPECT_THROW(settings.put(kAnna, in), Invariant);
}
