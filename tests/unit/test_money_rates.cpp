#include <gtest/gtest.h>

#include "money/Rates.hpp"

using Money::Rates::Rate;

TEST(MoneyRates, CrossGoesThroughTheDollar) {
    const Rate kzt{"2026-10-03", "KZT", 450};
    const Rate thb{"2026-10-03", "THB", 33.5};
    const Rate usd{"2026-10-03", "USD", 1};
    EXPECT_DOUBLE_EQ(*Money::Rates::cross(usd, kzt), 450);
    EXPECT_DOUBLE_EQ(*Money::Rates::cross(kzt, usd), 1.0 / 450);
    EXPECT_NEAR(*Money::Rates::cross(thb, kzt), 450 / 33.5, 1e-9);
    EXPECT_FALSE(Money::Rates::cross(Rate{"", "XXX", 0}, kzt).has_value());
}

TEST(MoneyRates, ConvertCarriesTheOlderRateDate) {
    const Rate kzt{"2026-10-03", "KZT", 450};
    const Rate thb{"2026-10-01", "THB", 33.5};
    const auto c = Money::Rates::convert(955.75, &thb, &kzt);
    ASSERT_TRUE(c.has_value());
    EXPECT_NEAR(c->amount, 955.75 * 450 / 33.5, 1e-6);
    EXPECT_EQ(c->rate_date, "2026-10-01");
    EXPECT_FALSE(Money::Rates::convert(1, nullptr, &kzt).has_value());
}
