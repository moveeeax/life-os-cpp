/**
 * @file test_money_summary.cpp
 * @brief All accounts in one currency: the nearest earlier quote, a currency
 *        without a quote left unconverted (partial), the change split into
 *        what the ledger did and what the rates did.
 */

#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "money/Summary.hpp"

using Money::Summary::Quote;
using Money::Summary::Quotes;

namespace {
Quotes quotes() {
    Quotes q;
    q["KZT"] = {Quote{"2026-10-01", 500.0}, Quote{"2026-10-10", 490.0}};
    q["EUR"] = {Quote{"2026-10-01", 0.9}, Quote{"2026-10-12", 0.92}};
    return q;
}
}  // namespace

TEST(MoneySummary, TakesTheNearestEarlierQuote) {
    const auto q = quotes();
    EXPECT_DOUBLE_EQ(Money::Summary::nearest(q, "KZT", "2026-10-09")->per_usd, 500.0);
    EXPECT_EQ(Money::Summary::nearest(q, "KZT", "2026-10-11")->date, "2026-10-10");
    EXPECT_FALSE(Money::Summary::nearest(q, "KZT", "2026-09-30").has_value()) << "nothing before the first quote";
    EXPECT_FALSE(Money::Summary::nearest(q, "THB", "2026-10-11").has_value());
    EXPECT_DOUBLE_EQ(Money::Summary::nearest(q, "USD", "2020-01-01")->per_usd, 1.0) << "USD is always 1";
}

TEST(MoneySummary, ConvertsADayAndMarksAMissingQuotePartial) {
    const auto q = quotes();
    const std::map<std::string, double> balances{{"KZT", 980000.0}, {"USD", 100.0}, {"THB", 3000.0}};
    const auto d = Money::Summary::convert_day(balances, q, "USD", "2026-10-11");
    EXPECT_DOUBLE_EQ(d.total, 980000.0 / 490.0 + 100.0);
    EXPECT_TRUE(d.partial) << "THB has no quote";
    const auto in_eur = Money::Summary::convert_day({{"KZT", 490000.0}}, q, "EUR", "2026-10-11");
    EXPECT_DOUBLE_EQ(in_eur.total, 490000.0 / 490.0 * 0.9);
    EXPECT_FALSE(in_eur.partial);
}

TEST(MoneySummary, ADayBeforeTheFirstQuoteIsPartialNotAnError) {
    const auto d = Money::Summary::convert_day({{"KZT", 1000.0}, {"USD", 5.0}}, quotes(), "USD", "2026-09-15");
    EXPECT_DOUBLE_EQ(d.total, 5.0);
    EXPECT_TRUE(d.partial);
}

TEST(MoneySummary, TheChangeSplitAddsUp) {
    const auto q = quotes();
    const std::map<std::string, double> start{{"KZT", 500000.0}, {"USD", 100.0}};
    const std::map<std::string, double> end{{"KZT", 600000.0}, {"USD", 80.0}};
    const auto c = Money::Summary::split(start, end, q, "USD", "2026-10-01", "2026-10-12");
    const double total_start = 500000.0 / 500.0 + 100.0;
    const double total_end = 600000.0 / 490.0 + 80.0;
    EXPECT_NEAR(c.total, total_end - total_start, 1e-9);
    EXPECT_NEAR(c.ledger + c.rates, c.total, 1e-9);
    EXPECT_NEAR(c.ledger, (600000.0 - 500000.0) / 490.0 + (80.0 - 100.0), 1e-9) << "the ledger at the end rates";
    EXPECT_NEAR(c.rates, 500000.0 / 490.0 - 500000.0 / 500.0, 1e-9) << "the start balances re-priced";
}

TEST(MoneySummary, ACurrencyWithoutQuotesIsLeftOutOfTheSplit) {
    const auto c = Money::Summary::split(
        {{"THB", 100.0}, {"USD", 10.0}}, {{"THB", 500.0}, {"USD", 20.0}}, quotes(), "USD", "2026-10-01", "2026-10-12");
    EXPECT_DOUBLE_EQ(c.total, 10.0);
    EXPECT_DOUBLE_EQ(c.ledger, 10.0);
    EXPECT_DOUBLE_EQ(c.rates, 0.0);
}

TEST(MoneySummary, ConvertsAnAmountToTheTargetPerOneUnit) {
    // KZT per 1 USD is 490: an account in KZT shows "490 KZT per 1 USD".
    EXPECT_DOUBLE_EQ(*Money::Summary::rate_per_target(quotes(), "KZT", "USD", "2026-10-11"), 490.0);
    EXPECT_NEAR(*Money::Summary::rate_per_target(quotes(), "KZT", "EUR", "2026-10-11"), 490.0 / 0.9, 1e-9);
    EXPECT_FALSE(Money::Summary::rate_per_target(quotes(), "THB", "USD", "2026-10-11").has_value());
}

TEST(MoneySummary, TotalsDatedSpendsEachAtItsOwnDayRate) {
    const std::vector<Money::Summary::Spend> spends{
        {"KZT", "2026-10-05", 50000.0}, {"KZT", "2026-10-11", 49000.0}, {"USD", "2026-10-11", 10.0}};
    const auto t = Money::Summary::total_in(spends, quotes(), "USD");
    EXPECT_DOUBLE_EQ(t.amount, 50000.0 / 500.0 + 49000.0 / 490.0 + 10.0);
    EXPECT_FALSE(t.partial);
    EXPECT_EQ(t.rate_date, "2026-10-01") << "the earliest quote day used";
}

TEST(MoneySummary, ASpendWithoutARateMakesTheTotalPartial) {
    const std::vector<Money::Summary::Spend> spends{{"THB", "2026-10-05", 300.0}, {"KZT", "2026-10-11", 4900.0}};
    const auto t = Money::Summary::total_in(spends, quotes(), "USD");
    EXPECT_DOUBLE_EQ(t.amount, 4900.0 / 490.0);
    EXPECT_TRUE(t.partial);
    EXPECT_EQ(t.rate_date, "2026-10-10");
}

TEST(MoneySummary, SpendsInTheTargetAddNoRateDay) {
    const std::vector<Money::Summary::Spend> spends{{"EUR", "2026-10-05", 20.0}};
    const auto t = Money::Summary::total_in(spends, quotes(), "EUR");
    EXPECT_DOUBLE_EQ(t.amount, 20.0);
    EXPECT_TRUE(t.rate_date.empty());
    EXPECT_TRUE(Money::Summary::total_in(std::vector<Money::Summary::Spend>(), quotes(), "USD").rate_date.empty());
}
