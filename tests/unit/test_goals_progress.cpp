#include <limits>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "goals/Fields.hpp"
#include "goals/Progress.hpp"

using Goals::Progress::Checkin;
using Goals::Progress::compute;
using Goals::Progress::Counts;
using Goals::Progress::Goal;
using Goals::Progress::merge;
using Goals::Progress::values_of;

namespace {

Goal number_goal(double from, double to, const char* start, const char* due) {
    return Goal{"number", start, due, from, to, 0, ""};
}

}  // namespace

TEST(GoalsProgress, AFallingNumberBehindItsPace) {
    const Goal g = number_goal(93.0, 75.0, "2026-09-01", "2027-03-31");
    const std::vector<Checkin> log{{"2026-09-15", 92.4}, {"2026-10-06", 91.6}};
    const auto p = compute(g, "2026-10-08", log, Counts{0, 0});
    const double elapsed = 37.0 / 211.0;
    EXPECT_NEAR(p["elapsed"].get<double>(), elapsed, 1e-9);
    EXPECT_EQ(p["days_left"], 174);
    EXPECT_NEAR(p["progress"].get<double>(), (93.0 - 91.6) / 18.0, 1e-9);
    EXPECT_DOUBLE_EQ(p["current"].get<double>(), 91.6);
    EXPECT_EQ(p["current_date"], "2026-10-06");
    const double expected = 93.0 - 18.0 * elapsed;
    EXPECT_NEAR(p["expected"].get<double>(), expected, 1e-9);
    EXPECT_NEAR(p["gap"].get<double>(), -(91.6 - expected), 1e-9) << "falling: lower is ahead";
    EXPECT_LT(p["gap"].get<double>(), 0);
    EXPECT_EQ(p["pace"], "behind");
    EXPECT_NEAR(p["per_week"].get<double>(), (75.0 - 91.6) / (174.0 / 7.0), 1e-9);
    EXPECT_FALSE(p["stale"].get<bool>());
}

TEST(GoalsProgress, ARisingNumberAheadButNotUpdated) {
    const Goal g = number_goal(800000, 3000000, "2026-07-01", "2026-12-31");
    const auto p = compute(g, "2026-10-08", {{"2026-08-01", 1300000}, {"2026-09-20", 2050000}}, Counts{0, 0});
    EXPECT_NEAR(p["progress"].get<double>(), 1250000.0 / 2200000.0, 1e-9);
    EXPECT_GT(p["gap"].get<double>(), 0) << "rising: higher is ahead";
    EXPECT_EQ(p["pace"], "on_track");
    EXPECT_TRUE(p["stale"].get<bool>()) << "18 days since the last check-in";
}

TEST(GoalsProgress, WithoutCheckinsTheStartValueIsCurrent) {
    const Goal g = number_goal(10, 20, "2026-10-01", "2026-12-31");
    const auto p = compute(g, "2026-10-08", {}, Counts{0, 0});
    EXPECT_DOUBLE_EQ(p["current"].get<double>(), 10);
    EXPECT_EQ(p["current_date"], "2026-10-01");
    EXPECT_DOUBLE_EQ(p["progress"].get<double>(), 0);
}

TEST(GoalsProgress, TwoPercentOfTheSpanIsStillOnTrack) {
    const Goal g = number_goal(0, 100, "2026-01-01", "2026-01-11");  // 10 days
    // At day 5 the expected value is 50; 48.5 is 1.5 % behind, 47 is 3 % behind.
    EXPECT_EQ(compute(g, "2026-01-06", {{"2026-01-06", 48.5}}, Counts{0, 0})["pace"], "on_track");
    EXPECT_EQ(compute(g, "2026-01-06", {{"2026-01-06", 47}}, Counts{0, 0})["pace"], "behind");
}

TEST(GoalsProgress, PastTheDueDate) {
    const Goal g = number_goal(0, 10, "2026-01-01", "2026-02-01");
    const auto p = compute(g, "2026-03-01", {{"2026-02-20", 6}}, Counts{0, 0});
    EXPECT_DOUBLE_EQ(p["elapsed"].get<double>(), 1);
    EXPECT_EQ(p["days_left"], 0);
    EXPECT_DOUBLE_EQ(p["per_week"].get<double>(), 4) << "at least one week left in the division";
}

TEST(GoalsProgress, StepsAgainstTheTimeGone) {
    const Goal g{"steps", "2026-01-01", "2026-01-21", 0, 0, 0, ""};  // 20 days
    EXPECT_EQ(compute(g, "2026-01-06", {}, Counts{4, 16})["pace"], "on_track") << "25 % done at 25 % time";
    EXPECT_EQ(compute(g, "2026-01-06", {}, Counts{7, 32})["pace"], "on_track") << "21.9 % is within 5 points of 25 %";
    EXPECT_EQ(compute(g, "2026-01-06", {}, Counts{3, 16})["pace"], "behind") << "18.75 % is more than 5 points behind";
    EXPECT_EQ(compute(g, "2026-01-06", {}, Counts{2, 16})["pace"], "behind");
    const auto p = compute(g, "2026-01-06", {}, Counts{0, 0});
    EXPECT_DOUBLE_EQ(p["progress"].get<double>(), 0);
    EXPECT_EQ(p["total"], 0);
}

TEST(GoalsProgress, CountAgainstTheExpectedShare) {
    const Goal g{"count", "2026-01-01", "2026-12-31", 0, 0, 12, ""};
    const auto behind = compute(g, "2026-10-08", {}, Counts{7, 9});
    EXPECT_EQ(behind["pace"], "behind") << "9.2 expected by 8 October";
    EXPECT_FALSE(behind["reached"].get<bool>());
    const auto reached = compute(g, "2026-10-08", {}, Counts{12, 12});
    EXPECT_TRUE(reached["reached"].get<bool>());
    EXPECT_DOUBLE_EQ(reached["progress"].get<double>(), 1);
    EXPECT_EQ(reached["target"], 12);
}

TEST(GoalsProgress, BinaryByTheDaysLeftAndTheResult) {
    Goal g{"binary", "2026-09-15", "2026-10-18", 0, 0, 0, ""};
    const auto open = compute(g, "2026-10-08", {}, Counts{0, 0});
    EXPECT_EQ(open["pace"], "behind") << "10 days left without a result";
    EXPECT_TRUE(open["progress"].is_null());
    g.due = "2026-12-31";
    EXPECT_EQ(compute(g, "2026-10-08", {}, Counts{0, 0})["pace"], "on_track");
    g.result = "pass";
    EXPECT_EQ(compute(g, "2026-10-08", {}, Counts{0, 0})["pace"], "passed");
    g.result = "fail";
    EXPECT_EQ(compute(g, "2026-10-08", {}, Counts{0, 0})["pace"], "failed");
}

TEST(GoalsFields, KnowsTheFourKinds) {
    EXPECT_TRUE(Goals::Fields::is_kind("binary"));
    EXPECT_FALSE(Goals::Fields::is_kind("habit"));
}

TEST(GoalsProgress, SourcePointsFeedCurrent) {
    // A weight goal on the scales: the latest day of the source on or before the date is current.
    const Goal g = number_goal(93.0, 75.0, "2026-09-01", "2027-03-31");
    const std::map<std::string, double> scales{{"2026-09-10", 92.0}, {"2026-10-05", 90.8}, {"2026-10-09", 90.1}};
    const auto points = merge(scales, {});
    ASSERT_EQ(points.size(), 3u);
    EXPECT_TRUE(points[0].from_source);
    const auto p = compute(g, "2026-10-08", values_of(points), Counts{0, 0});
    EXPECT_DOUBLE_EQ(p["current"].get<double>(), 90.8) << "a day after the date does not count";
    EXPECT_EQ(p["current_date"], "2026-10-05");
    EXPECT_FALSE(p["stale"].get<bool>());
}

TEST(GoalsProgress, CheckinWinsOnItsDate) {
    const std::map<std::string, double> scales{
        {"2026-10-01", 91.0}, {"2026-10-02", 90.9}, {"2026-10-03", std::numeric_limits<double>::quiet_NaN()}};
    const std::vector<Checkin> by_hand{{"2026-10-02", 90.0}, {"2026-10-04", 89.5}};
    const auto points = merge(scales, by_hand);
    ASSERT_EQ(points.size(), 3u) << "a day of the source without a number is no point";
    EXPECT_EQ(points[0].date, "2026-10-01");
    EXPECT_TRUE(points[0].from_source);
    EXPECT_EQ(points[1].date, "2026-10-02");
    EXPECT_DOUBLE_EQ(points[1].value, 90.0) << "the check-in, not the scales";
    EXPECT_FALSE(points[1].from_source);
    EXPECT_EQ(points[2].date, "2026-10-04");
    EXPECT_FALSE(points[2].from_source);
}

TEST(GoalsProgress, ASummedSourceCountsItsRunningTotal) {
    using Goals::Progress::running_totals;
    // "Earn 1 000 000" from 0 on practice income: 300 000, 200 000 and 100 000 on three
    // payment days give 300 000, 500 000, 600 000; current is 600 000, not the last 100 000.
    const Goal g = number_goal(0.0, 1000000.0, "2026-09-01", "2026-12-31");
    const std::map<std::string, double> income{
        {"2026-09-10", 300000.0}, {"2026-09-25", 200000.0}, {"2026-10-05", 100000.0}};
    const auto totals = running_totals(income, 0.0);
    ASSERT_EQ(totals.size(), 3u);
    EXPECT_DOUBLE_EQ(totals.at("2026-09-25"), 500000.0);
    const auto p = compute(g, "2026-10-08", values_of(merge(totals, {})), Counts{0, 0});
    EXPECT_DOUBLE_EQ(p["current"].get<double>(), 600000.0);
    EXPECT_DOUBLE_EQ(p["progress"].get<double>(), 0.6);
    EXPECT_EQ(p["current_date"], "2026-10-05");

    // From a start value: 10 000 steps done before the goal; a day without a number adds nothing.
    const std::map<std::string, double> steps{
        {"2026-10-01", 4000.0}, {"2026-10-02", std::numeric_limits<double>::quiet_NaN()}, {"2026-10-03", 6000.0}};
    const auto from_start = running_totals(steps, 10000.0);
    ASSERT_EQ(from_start.size(), 2u) << "a day without a number is no point";
    EXPECT_DOUBLE_EQ(from_start.at("2026-10-01"), 14000.0);
    EXPECT_DOUBLE_EQ(from_start.at("2026-10-03"), 20000.0);
    EXPECT_TRUE(running_totals({}, 5.0).empty());
}
