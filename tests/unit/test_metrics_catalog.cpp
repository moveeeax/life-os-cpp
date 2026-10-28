/**
 * @file test_metrics_catalog.cpp
 * @brief The catalog of daily series (src/metrics/Catalog.hpp): every spec id
 *        is there once, the legacy habit sources map to catalog ids, the
 *        parameters of money, practice and habit ids are checked, and an id
 *        parsed and written back is the same id.
 */

#include <set>
#include <string>

#include <gtest/gtest.h>

#include "metrics/Catalog.hpp"

namespace {

constexpr const char* kUuid = "0b0c3f6e-2a1d-4c55-9e1a-7d6f5b4a3c21";

}  // namespace

TEST(MetricsCatalog, CatalogHoldsEverySpecId) {
    const auto& all = Metrics::catalog();
    // 31 fitness, 7 food, 2 habits, 6 tape, 4 workout, 1 tasks, 4 money, 2 practice, 4 leetcode.
    EXPECT_EQ(all.size(), 61U);
    std::set<std::string> ids;
    for (const auto& d : all) {
        EXPECT_TRUE(ids.insert(std::string(d.id)).second) << "twice: " << d.id;
        EXPECT_EQ(std::string(d.id).substr(0, d.module.size() + 1), std::string(d.module) + ":") << d.id;
    }
    for (const char* id : {"leetcode:solved",
                           "fitness:steps",
                           "fitness:sleep",
                           "fitness:weight",
                           "fitness:metabolic_age",
                           "fitness:resting_hr",
                           "fitness:spo2_min",
                           "fitness:abnormal_hr",
                           "fitness:band_workout_distance",
                           "food:kcal",
                           "food:salt",
                           "habits:mood",
                           "habits:{habit}",
                           "tape:thigh",
                           "workout:volume",
                           "tasks:done",
                           "money:expense:{currency}",
                           "money:expense:{currency}:{category}",
                           "money:balance:{currency}",
                           "practice:income:{currency}"}) {
        EXPECT_EQ(ids.count(id), 1U) << id;
    }
    EXPECT_EQ(all.front().id, "fitness:steps");
    EXPECT_EQ(all.back().id, "practice:expense:{currency}");
    // Units the spec fixes.
    EXPECT_EQ(Metrics::find("fitness:sleep")->unit, "h");
    EXPECT_EQ(Metrics::find("fitness:sleep_awake")->unit, "min");
    EXPECT_EQ(Metrics::find("fitness:distance")->unit, "m");
    // Presence: last-of-day and counts, but not the night and not the balance.
    for (const char* id : {"fitness:weight",
                           "tape:waist",
                           "habits:mood",
                           "fitness:resting_hr",
                           "workout:sessions",
                           "fitness:band_workouts",
                           "tasks:done",
                           "fitness:abnormal_hr"}) {
        EXPECT_TRUE(Metrics::find(id)->presence) << id;
    }
    for (const char* id : {"fitness:sleep", "fitness:steps", "food:kcal", "money:balance:{currency}"}) {
        EXPECT_FALSE(Metrics::find(id)->presence) << id;
    }
}

TEST(MetricsCatalog, ParseMapsLegacyNames) {
    EXPECT_EQ(Metrics::canonical(*Metrics::parse("sleep")), "fitness:sleep");
    EXPECT_EQ(Metrics::canonical(*Metrics::parse("steps")), "fitness:steps");
    EXPECT_EQ(Metrics::canonical(*Metrics::parse("active_kcal")), "fitness:active_kcal");
    EXPECT_EQ(Metrics::canonical(*Metrics::parse("workout")), "workout:sessions");
    EXPECT_EQ(Metrics::canonical(*Metrics::parse("weight")), "fitness:weight");
    EXPECT_FALSE(Metrics::parse("manual")) << "manual is not a series";
}

TEST(MetricsCatalog, ParseParams) {
    const auto kzt = Metrics::parse("money:expense:KZT");
    ASSERT_TRUE(kzt);
    EXPECT_EQ(kzt->def->param, Metrics::Param::Currency);
    EXPECT_EQ(kzt->currency, "KZT");
    EXPECT_EQ(Metrics::unit_of(*kzt), "KZT");

    EXPECT_FALSE(Metrics::parse("money:expense:kzt"));
    EXPECT_FALSE(Metrics::parse("money:expense:KZTX"));
    EXPECT_FALSE(Metrics::parse("money:expense"));

    const auto cat = Metrics::parse(std::string("money:expense:KZT:") + kUuid);
    ASSERT_TRUE(cat);
    EXPECT_EQ(cat->def->param, Metrics::Param::CurrencyCategory);
    EXPECT_EQ(cat->category_id, kUuid);
    EXPECT_FALSE(Metrics::parse("money:expense:KZT:not-a-uuid"));
    EXPECT_FALSE(Metrics::parse(std::string("money:income:KZT:") + kUuid)) << "income has no category";

    const auto habit = Metrics::parse(std::string("habits:") + kUuid);
    ASSERT_TRUE(habit);
    EXPECT_EQ(habit->def->param, Metrics::Param::Habit);
    EXPECT_EQ(habit->habit_id, kUuid);
    EXPECT_EQ(Metrics::parse("habits:mood")->def->param, Metrics::Param::None);
    EXPECT_FALSE(Metrics::parse("habits:x"));

    EXPECT_FALSE(Metrics::parse("unknown:id"));
    EXPECT_FALSE(Metrics::parse(""));
    EXPECT_FALSE(Metrics::parse("fitness:steps:"));
    EXPECT_FALSE(Metrics::parse("fitness:{habit}"));
    EXPECT_FALSE(Metrics::parse("money:expense:{currency}")) << "a template is not an id";
    EXPECT_EQ(Metrics::parse("practice:income:USD")->def->gate, Metrics::Gate::Practice);
}

TEST(MetricsCatalog, CanonicalRoundTrips) {
    const std::string upper = "0B0C3F6E-2A1D-4C55-9E1A-7D6F5B4A3C21";
    EXPECT_EQ(Metrics::canonical(*Metrics::parse("habits:" + upper)), std::string("habits:") + kUuid)
        << "a uuid is kept lower-case";
    for (const std::string& id : {std::string("fitness:steps"),
                                  std::string("tape:waist"),
                                  std::string("money:expense:KZT"),
                                  std::string("money:expense:EUR:") + kUuid,
                                  std::string("money:balance:USD"),
                                  std::string("practice:expense:GBP"),
                                  std::string("habits:") + kUuid,
                                  std::string("habits:mood")}) {
        const auto r = Metrics::parse(id);
        ASSERT_TRUE(r) << id;
        EXPECT_EQ(Metrics::canonical(*r), id);
    }
    // Every entry without parameters is its own canonical id.
    for (const auto& d : Metrics::catalog()) {
        if (d.param == Metrics::Param::None) {
            EXPECT_EQ(Metrics::canonical(*Metrics::parse(d.id)), d.id);
        }
    }
    EXPECT_EQ(Metrics::canonical(Metrics::Ref{}), "");
}
