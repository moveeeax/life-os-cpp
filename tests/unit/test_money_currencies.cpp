/**
 * @file test_money_currencies.cpp
 * @brief The smallest unit of a currency: the table and the whole-units rule.
 */

#include <gtest/gtest.h>

#include "money/Currencies.hpp"

using Money::Currencies::fits;
using Money::Currencies::known;

TEST(MoneyCurrencies, KnownCodesCarryTheirSmallestUnit) {
    ASSERT_TRUE(known("BTC").has_value());
    EXPECT_EQ(known("BTC")->decimals, 8);
    EXPECT_EQ(known("BTC")->unit, "satoshi");
    EXPECT_EQ(known("RUB")->unit, "kopeck");
    EXPECT_EQ(known("KZT")->decimals, 2);
    EXPECT_EQ(known("VND")->decimals, 0);
    EXPECT_EQ(known("KWD")->decimals, 3);
    EXPECT_FALSE(known("XAU").has_value());
    for (const auto& m : Money::Currencies::kKnown) {
        EXPECT_GE(m.decimals, 0) << m.code;
        EXPECT_LE(m.decimals, 8) << m.code << ": the columns hold eight decimals";
        EXPECT_FALSE(m.unit.empty()) << m.code;
    }
}

TEST(MoneyCurrencies, AmountsAreWholeUnits) {
    EXPECT_TRUE(fits(4363.45, 2));
    EXPECT_TRUE(fits(0.1 + 0.2, 2)) << "a binary near-miss is still 30 units";
    EXPECT_FALSE(fits(1.234, 2));
    EXPECT_TRUE(fits(0.0057635, 8));
    EXPECT_TRUE(fits(0.00000001, 8));
    EXPECT_FALSE(fits(0.000000001, 8));
    EXPECT_TRUE(fits(25000, 0));
    EXPECT_FALSE(fits(100.5, 0));
    EXPECT_TRUE(fits(-53.25, 2)) << "an adjustment may be negative";
    EXPECT_TRUE(fits(999999999999.99, 2)) << "the largest amount the API takes";
}
