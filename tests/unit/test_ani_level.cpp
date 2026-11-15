/**
 * @file test_ani_level.cpp
 * @brief Ani's level rules (domain/AniLevel.hpp): the points of each scale for a
 *        day, the debt of attention, the weekly rules of the body scale, the
 *        thresholds and Ani's level as the lowest of the four.
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "domain/AniLevel.hpp"

namespace L = Domain::AniLevel;

namespace {

L::Day day(const std::string& date = "2026-11-10", const std::string& week = "2026-11-09") {
    L::Day d;
    d.date = date;
    d.week = week;
    d.days_in_month = 30;
    return d;
}

L::Targets targets() {
    L::Targets t;
    t.steps = 8000;
    t.kcal = 2000;
    t.protein_g = 100;
    t.water_ml = 2000;
    return t;
}

}  // namespace

TEST(AniLevelTest, ThresholdsGrowByAHundredPlusFiftyPerLevel) {
    EXPECT_EQ(L::threshold(1), 0);
    EXPECT_EQ(L::threshold(2), 150);
    EXPECT_EQ(L::threshold(3), 350);
    EXPECT_EQ(L::threshold(5), 900);
    EXPECT_EQ(L::threshold(10), 3150);
    EXPECT_EQ(L::threshold(20), 11400);
}

TEST(AniLevelTest, LevelIsTheHighestWhosePointsAreReached) {
    EXPECT_EQ(L::level_of(0), 1);
    EXPECT_EQ(L::level_of(149), 1);
    EXPECT_EQ(L::level_of(150), 2);
    EXPECT_EQ(L::level_of(899), 4);
    EXPECT_EQ(L::level_of(900), 5);
    EXPECT_EQ(L::level_of(11400), 20);
}

TEST(AniLevelTest, TasksCountTheShareClosedNotTheNumber) {
    L::Day d = day();
    d.due = 2;
    d.closed = 2;
    EXPECT_EQ(L::tasks_points(d), 30 + 10) << "all closed, nothing overdue";
    d.due = 10;
    d.closed = 10;
    EXPECT_EQ(L::tasks_points(d), 30 + 10) << "ten closed is worth what two are";
    d.closed = 5;
    d.overdue = 1;
    EXPECT_EQ(L::tasks_points(d), 15) << "half closed, something overdue";
}

TEST(AniLevelTest, ADayWithoutTasksDueIsMovedOnlyByFocus) {
    L::Day d = day();
    EXPECT_EQ(L::tasks_points(d), 0) << "no free points for nothing overdue";
    d.focus_minutes = 80;
    EXPECT_EQ(L::tasks_points(d), 30) << "three 25-minute blocks";
    d.focus_minutes = 400;
    d.focus_done = true;
    EXPECT_EQ(L::tasks_points(d), 60) << "focus of the day 30 and focus time capped at 30";
}

TEST(AniLevelTest, FoodPaysTheDiaryFirstAndKeepsAnHonestOverrun) {
    L::Day d = day();
    d.entries = 3;
    d.kcal = 3200;  // far over
    EXPECT_EQ(L::food_points(d, targets()), 40) << "a day written whole counts though the numbers are off";
    d.kcal = 2100;
    d.protein_g = 85;
    d.water_ml = 1200;
    EXPECT_EQ(L::food_points(d, targets()), 40 + 25 + 10 + 7);
    d.entries = 1;
    EXPECT_EQ(L::food_points(d, targets()), 20 + 25 + 10 + 7);
}

TEST(AniLevelTest, FoodWithoutAProfileSkipsTheTargetsItDoesNotHave) {
    L::Targets t;  // no kcal, no protein
    L::Day d = day();
    d.entries = 3;
    d.water_ml = 2000;
    EXPECT_EQ(L::food_points(d, t), 40 + 15);
}

TEST(AniLevelTest, BodyTakesSleepAndSteps) {
    L::Day d = day();
    d.sleep_minutes = 430;
    d.steps = 6500;
    EXPECT_EQ(L::body_points(d, targets()), 40 + 15);
    d.sleep_minutes = 370;
    d.steps = 8000;
    EXPECT_EQ(L::body_points(d, targets()), 20 + 30);
}

TEST(AniLevelTest, MoneyWithoutBudgetsEarnsOnlyTheCategorizedPart) {
    EXPECT_EQ(L::money_points(day(), targets()), 20);
}

TEST(AniLevelTest, MoneyChecksTheDaysShareOfTheMonthlyBudgetPerCurrency) {
    L::Targets t = targets();
    t.monthly_budget = {{"KZT", 300000.0}, {"USD", 600.0}};  // 10 000 and 20 a day
    L::Day d = day();
    EXPECT_EQ(L::money_points(d, t), 100) << "a day without spending is a day within the budget";
    d.spent = {{"KZT", 9000.0}, {"USD", 19.0}};
    EXPECT_EQ(L::money_points(d, t), 100);
    d.spent["USD"] = 25;
    EXPECT_EQ(L::money_points(d, t), 50) << "one currency over";
    d.spent["USD"] = 5;
    d.category_over = true;
    EXPECT_EQ(L::money_points(d, t), 70) << "a category past its budget";
    d.spent = {{"EUR", 500.0}};
    d.category_over = false;
    EXPECT_EQ(L::money_points(d, t), 100) << "a currency without a budget is not counted";
}

TEST(AniLevelTest, WorkoutsAreWorthThirtyFourDaysAWeekAtMost) {
    std::vector<L::Day> days;
    for (int i = 0; i < 6; ++i) {
        L::Day d = day("2026-11-1" + std::to_string(i), "2026-11-09");
        d.workouts = 1;
        d.band_data = true;
        days.push_back(d);
    }
    const auto r = L::score(days, targets());
    EXPECT_EQ(r.scales[L::kBody].points, 4 * 30);
}

TEST(AniLevelTest, TheFirstMeasureOfAWeekIsWorthTwentyOnce) {
    std::vector<L::Day> days;
    for (int i = 0; i < 3; ++i) {
        L::Day d = day("2026-11-1" + std::to_string(i), "2026-11-09");
        d.measured = true;
        d.band_data = true;
        days.push_back(d);
    }
    L::Day next = day("2026-11-16", "2026-11-16");
    next.measured = true;
    next.band_data = true;
    days.push_back(next);
    EXPECT_EQ(L::score(days, targets()).scales[L::kBody].points, 40);
}

TEST(AniLevelTest, ADebtHalvesTheDayAndGoesWithItsCause) {
    std::vector<L::Day> days;
    for (int i = 0; i < 4; ++i) {
        L::Day d = day("2026-11-1" + std::to_string(i));
        d.due = 1;
        d.closed = 1;
        d.overdue = i < 2 ? 1 : 0;
        d.overdue_long = i < 2 ? 1 : 0;  // a task more than 3 days past due, then done
        days.push_back(d);
    }
    const auto r = L::score(days, targets());
    // 30 + 0 (overdue) halved = 15, twice; then 30 + 10 twice.
    EXPECT_EQ(r.scales[L::kTasks].points, 15 + 15 + 40 + 40);
    EXPECT_FALSE(r.scales[L::kTasks].debt) << "the debt is gone on the last day";
}

TEST(AniLevelTest, FoodDebtComesAfterTwoEmptyDaysAndBodyDebtAfterThreeQuietOnes) {
    std::vector<L::Day> days;
    for (int i = 0; i < 3; ++i) {
        days.push_back(day("2026-11-1" + std::to_string(i)));
    }
    auto r = L::score(days, targets());
    EXPECT_TRUE(r.scales[L::kFood].debt);
    EXPECT_TRUE(r.scales[L::kBody].debt);
    days[2].entries = 3;
    days[2].band_data = true;
    r = L::score(days, targets());
    EXPECT_FALSE(r.scales[L::kFood].debt);
    EXPECT_FALSE(r.scales[L::kBody].debt);
}

TEST(AniLevelTest, WeekIsTheLastSevenDays) {
    std::vector<L::Day> days;
    for (int i = 0; i < 10; ++i) {
        L::Day d = day("2026-11-0" + std::to_string(i % 9 + 1));
        d.entries = i >= 3 ? 3 : 0;
        d.band_data = true;
        days.push_back(d);
    }
    const auto r = L::score(days, targets());
    EXPECT_EQ(r.scales[L::kFood].week, 7 * 40);
}

TEST(AniLevelTest, AniIsAsHighAsHerLowestScale) {
    L::Result r;
    r.scales[L::kTasks].points = 3150;  // 10
    r.scales[L::kBody].points = 900;    // 5
    r.scales[L::kFood].points = 5000;
    r.scales[L::kMoney].points = 150;  // 2
    EXPECT_EQ(L::overall(r), 2);
    r.scales[L::kMoney].points = 11400;
    EXPECT_EQ(L::overall(r), 5);
}
