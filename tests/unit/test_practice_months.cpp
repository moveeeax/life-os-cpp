/**
 * @file test_practice_months.cpp
 * @brief The calendar words of the practice module: a month as YYYY-MM and
 *        the day range it covers (December ends on the next year's first
 *        day), a year as YYYY, and the stages of a deal.
 */

#include <gtest/gtest.h>

#include "practice/Months.hpp"

TEST(PracticeMonths, AMonthIsItsDayRange) {
    const auto oct = Practice::Months::parse("2026-10");
    ASSERT_TRUE(oct.has_value());
    EXPECT_EQ(oct->first, "2026-10-01");
    EXPECT_EQ(oct->next, "2026-11-01");
    const auto dec = Practice::Months::parse("2026-12");
    ASSERT_TRUE(dec.has_value());
    EXPECT_EQ(dec->next, "2027-01-01") << "December ends on the next year's first day";
}

TEST(PracticeMonths, AnythingElseIsNoMonth) {
    for (const char* bad : {"",
                            "2026",
                            "2026-1",
                            "2026-13",
                            "2026-00",
                            "26-10",
                            "2026/10",
                            "2026-10-01",
                            "1899-12",
                            "2101-01",
                            "20a6-10",
                            "2026-1a"}) {
        EXPECT_FALSE(Practice::Months::parse(bad).has_value()) << bad;
    }
}

TEST(PracticeMonths, AYearAndItsRange) {
    EXPECT_EQ(Practice::Months::year_of("2026").value_or(0), 2026);
    EXPECT_FALSE(Practice::Months::year_of("26").has_value());
    EXPECT_FALSE(Practice::Months::year_of("1899").has_value());
    EXPECT_FALSE(Practice::Months::year_of("20x6").has_value());
    const auto y = Practice::Months::year_range(2026);
    EXPECT_EQ(y.first, "2026-01-01");
    EXPECT_EQ(y.next, "2027-01-01");
}

TEST(PracticeMonths, TheStagesOfADeal) {
    for (const char* s : {"lead", "negotiation", "active", "done", "lost"}) {
        EXPECT_TRUE(Practice::is_stage(s)) << s;
    }
    EXPECT_FALSE(Practice::is_stage("won"));
    EXPECT_FALSE(Practice::is_stage(""));
    EXPECT_EQ(Practice::kStages.front(), "lead") << "open stages first";
}
