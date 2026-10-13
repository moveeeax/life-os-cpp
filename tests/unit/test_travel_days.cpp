/**
 * @file test_travel_days.cpp
 * @brief Days by country from border crossings and the counters over them:
 *        a crossing day in both countries, a rolling window (Schengen 90/180)
 *        with its re-entry date, the 12-month tax window across New Year,
 *        a stay since the last entry. The worked numbers are in the comments.
 */

#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "travel/Days.hpp"
#include "travel/Schengen.hpp"

using Travel::Crossing;
using Travel::Rule;
using Travel::Threshold;
using Travel::Window;

namespace {

Travel::Day d(const char* text) {
    return Travel::day_of(text);
}

Rule rolling(std::set<std::string> countries, int window, int threshold) {
    return Rule{std::move(countries), Window::RollingDays, window, threshold, Threshold::AtMost, true};
}

}  // namespace

TEST(TravelDays, HomeBeforeTheFirstCrossing) {
    const auto days = Travel::days_by_country({}, "KZ", d("2026-01-01"), d("2026-01-03"));
    ASSERT_EQ(days.size(), 3U);
    for (const auto& [day, countries] : days) {
        EXPECT_EQ(countries, std::set<std::string>{"KZ"});
    }
}

TEST(TravelDays, ACrossingDayBelongsToBothCountries) {
    const std::vector<Crossing> c{{"2026-08-03", "KZ", "GE"}, {"2026-08-12", "GE", "KZ"}};
    const auto days = Travel::days_by_country(c, "KZ", d("2026-08-02"), d("2026-08-13"));
    EXPECT_EQ(days.at(d("2026-08-02")), (std::set<std::string>{"KZ"}));
    EXPECT_EQ(days.at(d("2026-08-03")), (std::set<std::string>{"KZ", "GE"}));
    EXPECT_EQ(days.at(d("2026-08-04")), (std::set<std::string>{"GE"}));
    EXPECT_EQ(days.at(d("2026-08-12")), (std::set<std::string>{"GE", "KZ"}));
    EXPECT_EQ(days.at(d("2026-08-13")), (std::set<std::string>{"KZ"}));
}

TEST(TravelDays, OutAndBackOnTheSameDay) {
    const std::vector<Crossing> c{{"2026-09-01", "KZ", "KG"}, {"2026-09-01", "KG", "KZ"}};
    const auto days = Travel::days_by_country(c, "KZ", d("2026-09-01"), d("2026-09-02"));
    EXPECT_EQ(days.at(d("2026-09-01")), (std::set<std::string>{"KZ", "KG"}));
    EXPECT_EQ(days.at(d("2026-09-02")), (std::set<std::string>{"KZ"})) << "home again after the day trip";
}

// The return is stored first (no time, or entered before the outbound): the day's
// crossings chain from where the owner woke up, so the next day is home.
TEST(TravelDays, ADayTripRecordedReturnFirst) {
    const std::vector<Crossing> c{{"2026-09-01", "KG", "KZ"}, {"2026-09-01", "KZ", "KG"}};
    const auto days = Travel::days_by_country(c, "KZ", d("2026-09-01"), d("2026-09-02"));
    EXPECT_EQ(days.at(d("2026-09-01")), (std::set<std::string>{"KZ", "KG"}));
    EXPECT_EQ(days.at(d("2026-09-02")), (std::set<std::string>{"KZ"})) << "home again, whatever the stored order";
}

// In France Jan 1 – Jan 30 2026, both crossing days counted: 30 days.
// On Feb 15 the 180-day window (2025-08-20 .. 2026-02-15) holds all 30: 30 used, 60 left.
TEST(TravelDays, SchengenUsedAndLeft) {
    const std::vector<Crossing> c{{"2026-01-01", "KZ", "FR"}, {"2026-01-30", "FR", "KZ"}};
    const auto r = Travel::count(rolling({"FR", "DE"}, 180, 90), c, "KZ", d("2026-02-15"));
    EXPECT_EQ(r.used, 30);
    EXPECT_EQ(r.left, 60);
    EXPECT_FALSE(r.reached_on.has_value()) << "outside the area: no stay is running";
}

// A stay starting Mar 1: on day k the window holds the 30 January days plus
// (k - Mar 1 + 1); at most 90 → 60 days (the 60th is Apr 29).
TEST(TravelDays, SchengenStayAllowedFromAPlannedDate) {
    const std::vector<Crossing> c{{"2026-01-01", "KZ", "FR"}, {"2026-01-30", "FR", "KZ"}};
    EXPECT_EQ(Travel::stay_allowed_from(rolling({"FR"}, 180, 90), c, "KZ", d("2026-02-15"), d("2026-03-01")), 60);
}

// The full 90 again: entering on D, while the window still starts in January
// day k holds (Jan 30 - (k - 179) + 1) + (k - D + 1) = Jan 30 - D + 181,
// which is ≤ 90 only for D ≥ Jan 30 + 91 = May 1.
// Entering Apr 30 instead: on Jun 29 (window from Jan 1) the 30 January days
// plus 61 stay days make 91, so that stay may last 60 days.
TEST(TravelDays, SchengenReentryForTheFullAllowance) {
    const std::vector<Crossing> c{{"2026-01-01", "KZ", "FR"}, {"2026-01-30", "FR", "KZ"}};
    const auto r = Travel::count(rolling({"FR"}, 180, 90), c, "KZ", d("2026-02-15"));
    ASSERT_TRUE(r.reentry.has_value());
    EXPECT_EQ(Travel::text_of(*r.reentry), "2026-05-01");
    EXPECT_EQ(Travel::stay_allowed_from(rolling({"FR"}, 180, 90), c, "KZ", d("2026-02-15"), d("2026-04-30")), 60);
}

// Inside since Mar 1, today Mar 10: 10 used; the 90th day is Mar 1 + 89 = May 29.
TEST(TravelDays, ARunningStayReachesTheLimitOn) {
    const std::vector<Crossing> c{{"2026-03-01", "KZ", "FR"}};
    const auto r = Travel::count(rolling({"FR"}, 180, 90), c, "KZ", d("2026-03-10"));
    EXPECT_EQ(r.used, 10);
    ASSERT_TRUE(r.reached_on.has_value());
    EXPECT_EQ(Travel::text_of(*r.reached_on), "2026-05-29");
}

// In Kazakhstan Oct 1 2025 – Mar 31 2026: 31+30+31+31+28+31 = 182 days.
// On Jun 30 2026 the window ending Mar 31 2026 holds all of them: 182, not over 183.
TEST(TravelDays, TaxWindowAcrossNewYear) {
    const Rule tax{{"KZ"}, Window::Rolling12MonthsInYear, 0, 183, Threshold::Over, true};
    const std::vector<Crossing> c{{"2025-10-01", "TH", "KZ"}, {"2026-03-31", "KZ", "TH"}};
    const auto r = Travel::count(tax, c, "TH", d("2026-06-30"));
    EXPECT_EQ(r.used, 182);
    EXPECT_EQ(r.left, 2) << "184 makes a resident";
    // Seen from Dec 31 2025 only the windows ending in 2025 count: Oct 1 – Dec 31 = 92.
    EXPECT_EQ(Travel::count(tax, c, "TH", d("2025-12-31")).used, 92);
}

TEST(TravelDays, TaxResidentAt184) {
    const Rule tax{{"KZ"}, Window::Rolling12MonthsInYear, 0, 183, Threshold::Over, true};
    const std::vector<Crossing> c{{"2025-10-01", "TH", "KZ"}, {"2026-04-02", "KZ", "TH"}};
    const auto r = Travel::count(tax, c, "TH", d("2026-06-30"));
    EXPECT_EQ(r.used, 184);
    EXPECT_EQ(r.left, 0);
}

// Russia counts the calendar year for the year's final status. Home Russia, in Kazakhstan
// Jan 12–18, Mar 7–14 and from May 24 on, crossing days in both: Jan 1–12 = 12,
// Jan 18 – Mar 7 = 14 + 28 + 7 = 49, Mar 14 – May 24 = 18 + 30 + 24 = 72 → 133.
TEST(TravelDays, CalendarYearCountsOnlyThisYear) {
    const Rule tax{{"RU"}, Window::CalendarYear, 0, 182, Threshold::Over, true};
    const std::vector<Crossing> c{{"2026-01-12", "RU", "KZ"},
                                  {"2026-01-18", "KZ", "RU"},
                                  {"2026-03-07", "RU", "KZ"},
                                  {"2026-03-14", "KZ", "RU"},
                                  {"2026-05-24", "RU", "KZ"}};
    const auto r = Travel::count(tax, c, "RU", d("2026-10-13"));
    EXPECT_EQ(r.used, 133) << "2025 at home does not count";
    EXPECT_EQ(r.left, 50) << "183 days make a resident";
    EXPECT_FALSE(r.reached_on.has_value()) << "outside Russia, nothing runs";
    EXPECT_FALSE(r.reentry.has_value());
}

// At home all year: on Mar 1 2026, Jan 1 – Mar 1 = 31 + 28 + 1 = 60 days; the 183rd day
// of the year is Jul 2 (31 + 28 + 31 + 30 + 31 + 30 = 181 by Jun 30).
TEST(TravelDays, CalendarYearReachedOn) {
    const Rule tax{{"RU"}, Window::CalendarYear, 0, 182, Threshold::Over, true};
    const auto r = Travel::count(tax, {}, "RU", d("2026-03-01"));
    EXPECT_EQ(r.used, 60);
    ASSERT_TRUE(r.reached_on.has_value());
    EXPECT_EQ(Travel::text_of(*r.reached_on), "2026-07-02");
}

// Entered Thailand Nov 1, today Nov 10: 10 used of 60; the 60th day is Nov 1 + 59 = Dec 30.
TEST(TravelDays, SinceTheLastEntry) {
    const Rule visa{{"TH"}, Window::SinceLastEntry, 0, 60, Threshold::AtMost, true};
    const std::vector<Crossing> c{{"2026-11-01", "KZ", "TH"}};
    const auto r = Travel::count(visa, c, "KZ", d("2026-11-10"));
    EXPECT_EQ(r.used, 10);
    EXPECT_EQ(r.left, 50);
    ASSERT_TRUE(r.reached_on.has_value());
    EXPECT_EQ(Travel::text_of(*r.reached_on), "2026-12-30");
    // After leaving, the next entry starts from zero.
    const std::vector<Crossing> left{{"2026-11-01", "KZ", "TH"}, {"2026-11-20", "TH", "KZ"}};
    EXPECT_EQ(Travel::count(visa, left, "KZ", d("2026-11-25")).used, 0);
}

// Georgia Aug 3 – Aug 12: 10 days with the crossing days, 8 full days without them.
TEST(TravelDays, CrossingDaysCanBeLeftOut) {
    const std::vector<Crossing> c{{"2026-08-03", "KZ", "GE"}, {"2026-08-12", "GE", "KZ"}};
    Rule r = rolling({"GE"}, 365, 365);
    EXPECT_EQ(Travel::count(r, c, "KZ", d("2026-09-01")).used, 10);
    r.count_crossing_days = false;
    EXPECT_EQ(Travel::count(r, c, "KZ", d("2026-09-01")).used, 8);
}

TEST(TravelDays, TheSchengenGroup) {
    EXPECT_TRUE(Travel::schengen().contains("AT"));
    EXPECT_TRUE(Travel::schengen().contains("FR"));
    EXPECT_FALSE(Travel::schengen().contains("GB"));
    EXPECT_FALSE(Travel::schengen().contains("KZ"));
}
