#include <gtest/gtest.h>

#include "money/Reports.hpp"

using Money::Reports::Budget;
using Money::Reports::Row;

namespace {

Row expense(const char* date,
            const char* currency,
            const char* category,
            double amount,
            const char* merchant = "",
            const char* flexibility = "variable") {
    return Row{date, currency, "expense", category, "expense", flexibility, merchant, amount};
}

Row income(const char* date, const char* currency, double amount) {
    return Row{date, currency, "income", "salary", "income", "variable", "", amount};
}

const Money::Period::Range kOctober{"2026-10-01", "2026-10-31"};

}  // namespace

TEST(MoneyReports, OneBlockPerCurrencyNeverSummedAcross) {
    const std::vector<Row> rows{
        expense("2026-10-02", "KZT", "food", 13275.61, "big c"),
        expense("2026-10-03", "KZT", "rent", 200000, "landlord", "fixed"),
        income("2026-10-01", "KZT", 500000),
        expense("2026-10-05", "THB", "food", 955.75, "big c"),
        expense("2026-10-06", "USD", "subs", 9.99, "apple"),
        Row{"2026-10-07", "KZT", "fx_adjustment", "", "", "variable", "", 120.5},
        Row{"2026-10-08", "KZT", "fx_adjustment", "", "", "variable", "", -20},
    };
    const std::map<std::string, Budget> budgets{{"food", {1000, "USD"}}, {"rent", {250000, "KZT"}}};
    const auto r = Money::Reports::build(kOctober, rows, {}, budgets, {}, "2026-10-10");
    ASSERT_EQ(r.blocks.size(), 3u);
    const auto& kzt = r.blocks[0];
    EXPECT_EQ(kzt.currency, "KZT");
    EXPECT_DOUBLE_EQ(kzt.income, 500000);
    EXPECT_DOUBLE_EQ(kzt.expense, 13275.61 + 200000 + 120.5 - 20) << "adjustments move the expense, signed";
    EXPECT_DOUBLE_EQ(kzt.net, 500000 - kzt.expense);
    EXPECT_DOUBLE_EQ(kzt.fixed_expense, 200000);
    EXPECT_NEAR(kzt.fixed_share, 200000 / kzt.expense, 1e-12);
    // 10 days elapsed of 31, 21 left
    EXPECT_NEAR(kzt.avg_daily, kzt.expense / 10, 1e-9);
    EXPECT_NEAR(kzt.projection, kzt.expense + kzt.avg_daily * 21, 1e-6);
    EXPECT_FALSE(kzt.prev_expense.has_value());
    EXPECT_FALSE(kzt.median3_expense.has_value());
    ASSERT_EQ(kzt.categories.size(), 2u);
    EXPECT_EQ(kzt.categories[0].category_id, "rent") << "largest first";
    EXPECT_DOUBLE_EQ(*kzt.categories[0].budget, 250000);
    EXPECT_DOUBLE_EQ(*kzt.categories[0].budget_share, 0.8);
    EXPECT_FALSE(kzt.categories[1].budget.has_value()) << "the food budget is in USD, not KZT";
    const auto& thb = r.blocks[1];
    EXPECT_EQ(thb.currency, "THB");
    EXPECT_DOUBLE_EQ(thb.expense, 955.75);
    EXPECT_FALSE(thb.categories[0].budget.has_value());
    EXPECT_EQ(r.blocks[2].currency, "USD");
    EXPECT_TRUE(r.recurring.empty());
    EXPECT_EQ(r.new_merchants, (std::vector<std::string>{"big c", "landlord", "apple"}));
}

TEST(MoneyReports, PreviousPeriodAndMedianOfThreeIncludeACurrencyOnlySeenBefore) {
    const std::vector<Row> now{expense("2026-10-02", "KZT", "food", 100)};
    const std::vector<std::vector<Row>> past{
        {expense("2026-09-02", "KZT", "food", 300), expense("2026-09-03", "THB", "food", 50)},
        {expense("2026-08-02", "KZT", "food", 100)},
        {expense("2026-07-02", "KZT", "food", 200)},
    };
    const auto r = Money::Reports::build(kOctober, now, past, {}, {"food"}, "2026-11-01");
    ASSERT_EQ(r.blocks.size(), 2u);
    EXPECT_DOUBLE_EQ(*r.blocks[0].prev_expense, 300);
    EXPECT_DOUBLE_EQ(*r.blocks[0].median3_expense, 200);
    EXPECT_EQ(r.blocks[1].currency, "THB");
    EXPECT_DOUBLE_EQ(r.blocks[1].expense, 0) << "shown with zero so the comparison is visible";
    EXPECT_DOUBLE_EQ(*r.blocks[1].prev_expense, 50);
    EXPECT_DOUBLE_EQ(*r.blocks[1].median3_expense, 0) << "two periods without rows count 0";
    EXPECT_DOUBLE_EQ(r.blocks[0].projection, 100) << "the period is over: nothing left to project";
}

TEST(MoneyReports, RecurringNeedsMonthlyGapsAndBreaksOnAShortOne) {
    const std::vector<Row> now{expense("2026-10-05", "USD", "subs", 9.99, "apple"),
                               expense("2026-10-05", "USD", "subs", 15, "netflix")};
    const std::vector<std::vector<Row>> past{
        {expense("2026-09-05", "USD", "subs", 9.99, "apple"), expense("2026-09-08", "USD", "subs", 15, "netflix")},
        {expense("2026-08-06", "USD", "subs", 9.99, "apple")},
        {expense("2026-07-07", "USD", "subs", 9.99, "apple")},
    };
    const auto r = Money::Reports::build(kOctober, now, past, {}, {"apple", "netflix"}, "2026-10-31");
    ASSERT_EQ(r.recurring.size(), 1u);
    EXPECT_EQ(r.recurring[0].merchant_key, "apple");
    EXPECT_EQ(r.recurring[0].times, 4);
    EXPECT_DOUBLE_EQ(r.recurring[0].amount, 9.99);
    EXPECT_EQ(r.recurring[0].last_date, "2026-10-05");
    EXPECT_EQ(r.recurring[0].next_expected, "2026-11-04");
    EXPECT_TRUE(r.new_merchants.empty());
}

TEST(MoneyReports, APeriodNotStartedProjectsOnlyWhatIsInIt) {
    const auto r =
        Money::Reports::build(kOctober, {expense("2026-10-01", "KZT", "rent", 300000)}, {}, {}, {}, "2026-09-20");
    ASSERT_EQ(r.blocks.size(), 1u);
    EXPECT_DOUBLE_EQ(r.blocks[0].projection, 300000);
}

TEST(MoneyReports, EmptyPeriodGivesNoBlocks) {
    const auto r = Money::Reports::build(kOctober, {}, {}, {}, {}, "2026-10-10");
    EXPECT_TRUE(r.blocks.empty());
    EXPECT_TRUE(r.recurring.empty());
}

TEST(MoneyReports, AnAdjustmentMovesTheCategoryOfItsExpense) {
    const std::vector<Row> rows{
        expense("2026-10-02", "KZT", "food", 10000, "big c"),
        expense("2026-10-03", "KZT", "rent", 200000, "landlord", "fixed"),
        // The repository hands an adjustment its expense row's category.
        Row{"2026-10-07", "KZT", "fx_adjustment", "food", "expense", "variable", "", 500},
        Row{"2026-10-08", "KZT", "fx_adjustment", "rent", "expense", "fixed", "", -1000},
        Row{"2026-10-09", "KZT", "fx_adjustment", "", "", "variable", "", 7},
    };
    const std::map<std::string, Budget> budgets{{"food", {20000, "KZT"}}};
    const auto r = Money::Reports::build(kOctober, rows, {}, budgets, {}, "2026-10-10");
    const auto& kzt = r.blocks.at(0);
    EXPECT_DOUBLE_EQ(kzt.expense, 10000 + 200000 + 500 - 1000 + 7);
    EXPECT_DOUBLE_EQ(kzt.fixed_expense, 199000);
    ASSERT_EQ(kzt.categories.size(), 2u) << "an adjustment without a category joins none";
    EXPECT_EQ(kzt.categories[1].category_id, "food");
    EXPECT_DOUBLE_EQ(kzt.categories[1].spent, 10500);
    EXPECT_DOUBLE_EQ(*kzt.categories[1].budget_share, 0.525);
}

TEST(MoneyReports, AWeekFindsMonthlyChargesInTheWindowItIsGiven) {
    const Money::Period::Range week{"2026-10-05", "2026-10-11"};
    const std::vector<Row> now{expense("2026-10-05", "USD", "subs", 9.99, "apple")};
    const std::vector<std::vector<Row>> past{{}, {}, {}};
    EXPECT_TRUE(Money::Reports::build(week, now, past, {}, {"apple"}, "2026-10-11").recurring.empty())
        << "four weeks hold one charge";
    const std::vector<Row> window{expense("2026-08-06", "USD", "subs", 9.99, "apple"),
                                  expense("2026-09-05", "USD", "subs", 9.99, "apple"),
                                  expense("2026-10-05", "USD", "subs", 9.99, "apple")};
    const auto r = Money::Reports::build(week, now, past, {}, {"apple"}, "2026-10-11", &window);
    ASSERT_EQ(r.recurring.size(), 1u);
    EXPECT_EQ(r.recurring[0].times, 3);
    EXPECT_EQ(r.recurring[0].next_expected, "2026-11-04");
}
