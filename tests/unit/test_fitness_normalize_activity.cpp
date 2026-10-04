/**
 * @file test_normalize_activity.cpp
 * @brief daily_activity normalization: minute dedup, calories, day bounds.
 *
 * Semantics copied verbatim from the reference iter_daily_activity: group by
 * local minute, on collision take the max of the tuple (steps, distance,
 * calories), suppressed steps count, calories from the separate key replace
 * the summed ones.
 */

#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "fitness/xiaomi/Normalize.hpp"

namespace {

using nlohmann::json;

json step_record(long long epoch, int steps, double distance, double calories, int zone_offset = 28800) {
    return json{{"time", epoch},
                {"zone_offset", zone_offset},
                {"zone_name", "Asia/Shanghai"},
                {"value", json{{"steps", steps}, {"distance", distance}, {"calories", calories}}.dump()}};
}

json calorie_record(long long epoch, double calories, int zone_offset = 28800) {
    return json{{"time", epoch},
                {"zone_offset", zone_offset},
                {"zone_name", "Asia/Shanghai"},
                {"value", json{{"calories", calories}}.dump()}};
}

// 2026-09-22 10:00:00 +08:00.
constexpr long long kMorning = 1790042400;

}  // namespace

TEST(NormalizeActivity, TwoDevicesInOneMinuteKeepTheLargerRecord) {
    // Same local minute, different seconds: the phone and the band send parallel
    // slices of the same activity. Summing would double count.
    const auto result = Xiaomi::normalize_daily_activity(
        {step_record(kMorning, 60, 40.0, 3.0), step_record(kMorning + 20, 40, 30.0, 2.0)}, {}, "1234567890");

    ASSERT_EQ(result.days.size(), 1u);
    EXPECT_EQ(result.days[0].steps, 60);
    EXPECT_DOUBLE_EQ(result.days[0].distance_m.value(), 40.0);
    EXPECT_EQ(result.suppressed_steps, 40);
    EXPECT_EQ(result.skipped, 0);
}

TEST(NormalizeActivity, EqualTuplesKeepExactlyOne) {
    const auto result = Xiaomi::normalize_daily_activity(
        {step_record(kMorning, 50, 35.0, 2.5), step_record(kMorning + 30, 50, 35.0, 2.5)}, {}, "1234567890");

    ASSERT_EQ(result.days.size(), 1u);
    EXPECT_EQ(result.days[0].steps, 50);
    EXPECT_EQ(result.suppressed_steps, 50);
}

TEST(NormalizeActivity, DifferentMinutesAreSummed) {
    const auto result = Xiaomi::normalize_daily_activity(
        {step_record(kMorning, 60, 40.0, 3.0), step_record(kMorning + 60, 40, 30.0, 2.0)}, {}, "1234567890");

    ASSERT_EQ(result.days.size(), 1u);
    EXPECT_EQ(result.days[0].steps, 100);
    EXPECT_DOUBLE_EQ(result.days[0].distance_m.value(), 70.0);
    EXPECT_EQ(result.suppressed_steps, 0);
}

TEST(NormalizeActivity, CaloriesKeyReplacesSummedCalories) {
    // The cloud's calories key replaces the sum from step records, not adds to it.
    const auto result = Xiaomi::normalize_daily_activity(
        {step_record(kMorning, 60, 40.0, 3.0), step_record(kMorning + 60, 40, 30.0, 2.0)},
        {calorie_record(kMorning, 200.0), calorie_record(kMorning + 3600, 149.0)},
        "1234567890");

    ASSERT_EQ(result.days.size(), 1u);
    EXPECT_DOUBLE_EQ(result.days[0].active_kcal.value(), 349.0);
}

TEST(NormalizeActivity, Pre2000TimestampIsSkippedNotFatal) {
    const auto result = Xiaomi::normalize_daily_activity(
        {step_record(100, 60, 40.0, 3.0), step_record(kMorning, 40, 30.0, 2.0)}, {}, "1234567890");

    ASSERT_EQ(result.days.size(), 1u);
    EXPECT_EQ(result.days[0].steps, 40);
    EXPECT_EQ(result.skipped, 1);
}

TEST(NormalizeActivity, EmptyInputGivesEmptyOutput) {
    const auto result = Xiaomi::normalize_daily_activity({}, {}, "1234567890");
    EXPECT_TRUE(result.days.empty());
    EXPECT_EQ(result.suppressed_steps, 0);
    EXPECT_EQ(result.skipped, 0);
}

// The day is computed in the record's timezone. 2026-09-22 23:30 UTC is already
// the 23rd in +08:00, and a mistake here shifts daily aggregates, the
// reference's main invariant.
TEST(NormalizeActivity, DayBoundaryFollowsRecordZoneNotUtc) {
    const long long late_utc = 1790119800;  // 2026-09-22T23:30:00Z
    const auto result = Xiaomi::normalize_daily_activity({step_record(late_utc, 10, 5.0, 1.0)}, {}, "1234567890");

    ASSERT_EQ(result.days.size(), 1u);
    EXPECT_EQ(result.days[0].date, "2026-09-23");
    EXPECT_EQ(result.days[0].timezone, "Asia/Shanghai");
}
