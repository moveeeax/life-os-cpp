#include <gtest/gtest.h>

#include "money/Merchant.hpp"

TEST(MoneyMerchant, KeyFoldsCaseSpacingAndPunctuation) {
    EXPECT_EQ(Money::merchant_key("Sukiya Phuket  (THB)"), "sukiya phuket thb");
    EXPECT_EQ(Money::merchant_key("  SUKIYA-PHUKET/THB "), "sukiya phuket thb");
    EXPECT_EQ(Money::merchant_key("M&M's"), "m&m's");
    EXPECT_EQ(Money::merchant_key("7-Eleven #1234"), "7 eleven 1234");
    EXPECT_EQ(Money::merchant_key(""), "");
    EXPECT_EQ(Money::merchant_key("***"), "");
}

TEST(MoneyMerchant, KeyKeepsOtherScripts) {
    EXPECT_EQ(Money::merchant_key("Магнит, Алматы"), "Магнит Алматы");
    EXPECT_EQ(Money::merchant_key("เซเว่น 7"), "เซเว่น 7");
}
