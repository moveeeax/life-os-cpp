/**
 * @file test_food_goals.cpp
 * @brief Goal arithmetic: Mifflin-St Jeor, activity, deficit floor, macros,
 *        overrides, age.
 */

#include <stdexcept>

#include <gtest/gtest.h>

#include "food/Goals.hpp"

namespace {

Food::Goals::Inputs owner() {
    // The owner's profile from the design note: 91.9 kg, 170 cm, 35 years,
    // male, light activity, target 75 kg at 0.75 kg per week.
    return {91.9, 170, 35, "male", "light", 75.0, 0.75};
}

}  // namespace

TEST(FoodGoals, OwnerExampleMatchesTheSpec) {
    const auto c = Food::Goals::compute(owner());
    EXPECT_EQ(c.bmr, 1812);          // 919 + 1062.5 - 175 + 5 = 1811.5
    EXPECT_EQ(c.maintenance, 2492);  // 1812 * 1.375 = 2491.5
    EXPECT_EQ(c.deficit, 825);       // 0.75 * 7700 / 7
    EXPECT_EQ(c.kcal, 1667);
    EXPECT_FALSE(c.floored);
    EXPECT_TRUE(c.below_bmr) << "the owner's own target sits below the BMR; allowed, flagged";
    EXPECT_EQ(c.protein_g, 135);  // 1.8 * 75
    EXPECT_EQ(c.fat_g, 46);       // 0.25 * 1667 / 9 = 46.3
    EXPECT_EQ(c.carbs_g, 178);    // (1667 - 540 - 414) / 4 = 178.25
}

TEST(FoodGoals, FemaleUsesMinus161) {
    auto in = owner();
    in.sex = "female";
    EXPECT_EQ(Food::Goals::compute(in).bmr, 1646);  // 1811.5 - 166 = 1645.5
}

TEST(FoodGoals, DeficitIsFlooredAtTheMinimum) {
    Food::Goals::Inputs in{50.0, 160, 60, "female", "sedentary", 45.0, 1.5};
    const auto c = Food::Goals::compute(in);
    // bmr 500 + 1000 - 300 - 161 = 1039; maintenance 1247; deficit 1650.
    EXPECT_EQ(c.bmr, 1039);
    EXPECT_TRUE(c.floored);
    EXPECT_EQ(c.kcal, Food::Goals::kMinKcal);
    EXPECT_FALSE(c.below_bmr);
    EXPECT_GE(c.carbs_g, 0);
}

TEST(FoodGoals, GainHasNegativeDeficitAndMaintainHasNone) {
    auto gain = owner();
    gain.pace_kg_per_week = -0.5;
    const auto g = Food::Goals::compute(gain);
    EXPECT_EQ(g.deficit, -550);
    EXPECT_EQ(g.kcal, g.maintenance + 550);

    auto keep = owner();
    keep.pace_kg_per_week = 0;
    const auto k = Food::Goals::compute(keep);
    EXPECT_EQ(k.deficit, 0);
    EXPECT_EQ(k.kcal, k.maintenance);
}

TEST(FoodGoals, CarbsNeverGoNegative) {
    // A tiny goal with a heavy protein target: the remainder is clamped.
    Food::Goals::Inputs in{45.0, 150, 80, "female", "sedentary", 120.0, 1.5};
    EXPECT_EQ(Food::Goals::compute(in).carbs_g, 0);
}

TEST(FoodGoals, AgeCountsWholeYears) {
    EXPECT_EQ(Food::Goals::age_on("1991-03-15", "2026-03-15"), 35);
    EXPECT_EQ(Food::Goals::age_on("1991-03-15", "2026-03-14"), 34);
    EXPECT_EQ(Food::Goals::age_on("1992-02-29", "2026-02-28"), 33);
    EXPECT_EQ(Food::Goals::age_on("1992-02-29", "2026-03-01"), 34);
    EXPECT_THROW(Food::Goals::age_on("2027-01-01", "2026-10-06"), std::invalid_argument);
    EXPECT_THROW(Food::Goals::age_on("1991-13-01", "2026-10-06"), std::invalid_argument);
    EXPECT_THROW(Food::Goals::age_on("yesterday", "2026-10-06"), std::invalid_argument);
}

TEST(FoodGoals, OverridesReplaceOnlyTheirNumber) {
    const auto c = Food::Goals::compute(owner());
    const auto t = Food::Goals::apply_overrides(c, 1670, std::nullopt, 60, std::nullopt);
    EXPECT_EQ(t.kcal, 1670);
    EXPECT_EQ(t.protein_g, c.protein_g);
    EXPECT_EQ(t.fat_g, 60);
    EXPECT_EQ(t.carbs_g, c.carbs_g) << "carbs are not re-derived from the other overrides";
}

TEST(FoodGoals, UnknownActivityOrSexThrows) {
    auto in = owner();
    in.activity = "couch";
    EXPECT_THROW(Food::Goals::compute(in), std::invalid_argument);
    in = owner();
    in.sex = "other";
    EXPECT_THROW(Food::Goals::compute(in), std::invalid_argument);
    EXPECT_DOUBLE_EQ(Food::Goals::activity_factor("very_active"), 1.9);
}
