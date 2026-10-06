#include <gtest/gtest.h>

#include "money/Period.hpp"

using Money::Period::Kind;
using Money::Period::Range;

TEST(MoneyPeriod, WeekRunsMondayToSundayAcrossAMonthEnd) {
    const Range w = Money::Period::of(Kind::week, "2026-10-01");  // a Thursday
    EXPECT_EQ(w.from, "2026-09-28");
    EXPECT_EQ(w.to, "2026-10-04");
    EXPECT_EQ(Money::Period::of(Kind::week, "2026-10-05").from, "2026-10-05") << "a Monday starts its own week";
    EXPECT_EQ(Money::Period::of(Kind::week, "2026-10-11").from, "2026-10-05") << "a Sunday ends the week";
    EXPECT_EQ(Money::Period::days(w), 7);
}

TEST(MoneyPeriod, MonthAndQuarter) {
    EXPECT_EQ(Money::Period::of(Kind::month, "2026-02-10").to, "2026-02-28");
    EXPECT_EQ(Money::Period::of(Kind::month, "2028-02-10").to, "2028-02-29");
    EXPECT_EQ(Money::Period::of(Kind::month, "2026-12-31").from, "2026-12-01");
    const Range q = Money::Period::of(Kind::quarter, "2026-11-15");
    EXPECT_EQ(q.from, "2026-10-01");
    EXPECT_EQ(q.to, "2026-12-31");
    EXPECT_EQ(Money::Period::of(Kind::quarter, "2026-03-31").from, "2026-01-01");
    EXPECT_EQ(Money::Period::days(q), 92);
}

TEST(MoneyPeriod, CustomAndPrevious) {
    const Range c = Money::Period::custom("2026-10-03", "2026-10-12");
    EXPECT_EQ(Money::Period::days(c), 10);
    EXPECT_THROW(Money::Period::custom("2026-10-12", "2026-10-03"), std::invalid_argument);
    EXPECT_THROW(Money::Period::of(Kind::custom, "2026-10-03"), std::invalid_argument);
    EXPECT_THROW(Money::Period::of(Kind::week, "2026-02-30"), std::invalid_argument);

    const Range pc = Money::Period::previous(Kind::custom, c);
    EXPECT_EQ(pc.from, "2026-09-23");
    EXPECT_EQ(pc.to, "2026-10-02");
    const Range pm = Money::Period::previous(Kind::month, Money::Period::of(Kind::month, "2026-01-15"));
    EXPECT_EQ(pm.from, "2025-12-01");
    EXPECT_EQ(pm.to, "2025-12-31");
    const Range pq = Money::Period::previous(Kind::quarter, Money::Period::of(Kind::quarter, "2026-01-15"));
    EXPECT_EQ(pq.from, "2025-10-01");
    const Range pw = Money::Period::previous(Kind::week, Money::Period::of(Kind::week, "2026-10-05"));
    EXPECT_EQ(pw.from, "2026-09-28");
    EXPECT_EQ(pw.to, "2026-10-04");
}

TEST(MoneyPeriod, DaysLeft) {
    const Range m = Money::Period::of(Kind::month, "2026-10-01");
    EXPECT_EQ(Money::Period::days_left(m, "2026-10-01"), 31);
    EXPECT_EQ(Money::Period::days_left(m, "2026-10-31"), 1);
    EXPECT_EQ(Money::Period::days_left(m, "2026-11-01"), 0);
    EXPECT_EQ(Money::Period::days_left(m, "2026-09-01"), 31) << "before the start: the whole period";
}
