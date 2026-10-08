#include <gtest/gtest.h>

#include "goals/Fields.hpp"
#include "goals/Progress.hpp"

using Goals::Progress::Checkin;
using Goals::Progress::compute;
using Goals::Progress::Counts;
using Goals::Progress::Goal;

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
