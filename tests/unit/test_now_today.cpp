/**
 * @file test_now_today.cpp
 * @brief The day's digest of the Now screen: the parts that may fail alone,
 *        and the per-section blocks built from what the sections compute.
 */

#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "now/Today.hpp"
#include "tasks/Agenda.hpp"

using json = nlohmann::json;
using Now::Today::Part;

TEST(NowToday, AssembleRunsEnabledPartsOnly) {
    const std::vector<Part> parts = {
        {"tasks",
         true,
         [] {
             return json{{"late_count", 0}};
         }},
        {"goals", false, [] { return json::array(); }},
        {"food", true, [] { return json(); }},
    };
    const json out = Now::Today::assemble(parts, json());
    EXPECT_TRUE(out.contains("tasks"));
    EXPECT_FALSE(out.contains("goals")) << "a disabled module is absent";
    EXPECT_FALSE(out.contains("food")) << "a part with nothing to show is absent";
    EXPECT_TRUE(out["focus"].is_null());
    EXPECT_EQ(out["errors"], json::array());
}

TEST(NowToday, AThrowingPartLandsInErrors) {
    const std::vector<Part> parts = {
        {"money", true, []() -> json { throw std::runtime_error("db down"); }},
        {"tasks",
         true,
         [] {
             return json{{"late_count", 1}};
         }},
    };
    const json out = Now::Today::assemble(parts, json{{"text", "Ship"}});
    EXPECT_FALSE(out.contains("money"));
    EXPECT_EQ(out["errors"], json::array({"money"}));
    EXPECT_EQ(out["tasks"]["late_count"], 1);
    EXPECT_EQ(out["focus"]["text"], "Ship");
}

TEST(NowToday, TasksBlockTakesTheOldestLateAndTodaysOnly) {
    ::Tasks::Agenda::Groups g;
    g.late = {json{{"id", "b"}, {"title", "Newer late"}, {"due", "2026-10-10"}},
              json{{"id", "a"}, {"title", "Pay the rent"}, {"due", "2026-10-01"}}};
    g.soon = {json{{"id", "c"}, {"title", "Call the bank"}, {"due", "2026-10-12"}},
              json{{"id", "d"}, {"title", "Tomorrow"}, {"due", "2026-10-13"}}};
    const json b = Now::Today::tasks_block(g, "2026-10-12", 3);
    EXPECT_EQ(b["late_count"], 2);
    EXPECT_EQ(b["first_late"]["id"], "a");
    EXPECT_EQ(b["first_late"]["title"], "Pay the rent");
    EXPECT_EQ(b["first_late"]["due"], "2026-10-01");
    ASSERT_EQ(b["today"].size(), 1u);
    EXPECT_EQ(b["today"][0], (json{{"id", "c"}, {"title", "Call the bank"}, {"status", "open"}}));
    EXPECT_EQ(b["inbox_count"], 3);
}

TEST(NowToday, TasksBlockWithNothingLate) {
    const json b = Now::Today::tasks_block(::Tasks::Agenda::Groups{}, "2026-10-12", 0);
    EXPECT_EQ(b["late_count"], 0);
    EXPECT_TRUE(b["first_late"].is_null());
    EXPECT_EQ(b["today"], json::array());
}

TEST(NowToday, GoalsBlockPutsBehindFirstAndDropsTheDecided) {
    const json goals = json::array({
        {{"id", "1"}, {"title", "Run"}, {"kind", "steps"}, {"due", "2026-12-01"}, {"progress", {{"pace", "on_track"}}}},
        {{"id", "2"}, {"title", "Books"}, {"kind", "count"}, {"due", "2026-12-31"}, {"progress", {{"pace", "behind"}}}},
        {{"id", "3"}, {"title", "Exam"}, {"kind", "binary"}, {"due", "2026-11-01"}, {"progress", {{"pace", "passed"}}}},
        {{"id", "4"},
         {"title", "Weight"},
         {"kind", "number"},
         {"due", "2026-11-15"},
         {"progress", {{"pace", "behind"}}}},
        {{"id", "5"}, {"title", "Cert"}, {"kind", "binary"}, {"due", "2026-11-02"}, {"progress", {{"pace", "failed"}}}},
    });
    const json b = Now::Today::goals_block(goals);
    ASSERT_EQ(b.size(), 3u);
    EXPECT_EQ(b[0]["id"], "4");
    EXPECT_EQ(b[1]["id"], "2");
    EXPECT_EQ(b[2]["id"], "1");
    EXPECT_EQ(b[0],
              (json{{"id", "4"},
                    {"title", "Weight"},
                    {"kind", "number"},
                    {"due", "2026-11-15"},
                    {"pace", "behind"},
                    {"progress", nullptr},
                    {"days_left", nullptr},
                    {"unit", ""},
                    {"detail", {{"pace", "behind"}}}}));
}

TEST(NowToday, MoneyBlockKeepsEachCurrencyApart) {
    const json today_rows = json::array({
        {{"currency", "KZT"}, {"type", "expense"}, {"amount", 5000}},
        {{"currency", "KZT"}, {"type", "expense"}, {"amount", 2500}},
        {{"currency", "THB"}, {"type", "expense"}, {"amount", 300}},
        {{"currency", "KZT"}, {"type", "income"}, {"amount", 900000}},
    });
    const json report = {{"blocks",
                          json::array({{{"currency", "KZT"}, {"projection", 412000}, {"median3_expense", 350000}},
                                       {{"currency", "USD"}, {"projection", 40}, {"median3_expense", nullptr}}})}};
    const json b = Now::Today::money_block(today_rows, report);
    ASSERT_EQ(b.size(), 3u);
    EXPECT_EQ(b[0], (json{{"currency", "KZT"}, {"today", 7500.0}, {"projection", 412000}, {"median3", 350000}}));
    EXPECT_EQ(b[1], (json{{"currency", "THB"}, {"today", 300.0}, {"projection", nullptr}, {"median3", nullptr}}));
    EXPECT_EQ(b[2], (json{{"currency", "USD"}, {"today", 0.0}, {"projection", 40}, {"median3", nullptr}}));
}

TEST(NowToday, MoneyBlockIsNullWhenThereIsNothing) {
    EXPECT_TRUE(Now::Today::money_block(json::array(), json{{"blocks", json::array()}}).is_null());
}

TEST(NowToday, TasksBlockListsUpToFiveLateOldestFirstAndTodaysStatus) {
    ::Tasks::Agenda::Groups g;
    for (int i = 0; i < 7; ++i) {
        g.late.push_back(json{{"id", std::to_string(i)},
                              {"title", "Late " + std::to_string(i)},
                              {"due", "2026-10-0" + std::to_string(9 - i)}});
    }
    g.soon = {json{{"id", "t"}, {"title", "Today"}, {"due", "2026-10-12"}, {"status", "in_progress"}}};
    const json b = Now::Today::tasks_block(g, "2026-10-12", 0);
    ASSERT_EQ(b["late"].size(), 5u);
    EXPECT_EQ(b["late"][0]["due"], "2026-10-03");
    EXPECT_EQ(b["late"][4]["due"], "2026-10-07");
    EXPECT_EQ(b["late_count"], 7);
    EXPECT_EQ(b["today"][0]["status"], "in_progress");
}

TEST(NowToday, GoalsBlockCarriesProgressAndDaysLeft) {
    const json goals = json::array({{{"id", "1"},
                                     {"title", "Books"},
                                     {"kind", "count"},
                                     {"due", "2026-12-31"},
                                     {"progress", {{"pace", "on_track"}, {"progress", 0.58}, {"days_left", 80}}}}});
    const json b = Now::Today::goals_block(goals);
    EXPECT_DOUBLE_EQ(b[0]["progress"].get<double>(), 0.58);
    EXPECT_EQ(b[0]["days_left"], 80);
}

TEST(NowToday, GoalsBlockCarriesTheUnitAndTheWholeProgress) {
    const json progress = {{"pace", "behind"}, {"progress", 0.4}, {"days_left", 30}, {"expected", 6}, {"done", 4}};
    const json goals = json::array({{{"id", "1"},
                                     {"title", "Books"},
                                     {"kind", "count"},
                                     {"unit", "books"},
                                     {"due", "2026-12-31"},
                                     {"progress", progress}},
                                    {{"id", "2"},
                                     {"title", "Run"},
                                     {"kind", "steps"},
                                     {"unit", nullptr},
                                     {"due", "2026-12-31"},
                                     {"progress", {{"pace", "on_track"}, {"progress", 0.5}, {"days_left", 30}}}}});
    const json b = Now::Today::goals_block(goals);
    EXPECT_EQ(b[0]["unit"], "books");
    EXPECT_EQ(b[0]["detail"], progress) << "the page words need gap, expected, done, stale, reached";
    EXPECT_EQ(b[1]["unit"], "") << "a missing unit reads as none";
}

namespace {
json sleep_row(const char* end, int asleep, bool nap) {
    return json{{"start_at", "2026-10-11T18:41:00Z"},
                {"end_at", end},
                {"time_asleep_minutes", asleep},
                {"is_nap", nap},
                {"stages", json::array({{{"stage", "deep"}, {"minutes", 75}}, {{"stage", "rem"}, {"minutes", 91}}})}};
}
json weight(const char* ts, double kg) {
    return json{{"timestamp", ts}, {"weight_kg", kg}};
}
}  // namespace

TEST(NowToday, HealthBlockFromAFullDay) {
    const json summary = {{"steps", 6120}, {"active_kcal", 370}, {"resting_bpm", 58}};
    const json sleeps =
        json::array({sleep_row("2026-10-12T01:58:00Z", 412, false), sleep_row("2026-10-12T09:30:00Z", 25, true)});
    json body = json::array();
    const char* days[] = {"2026-10-04T05:00:00Z",
                          "2026-10-05T05:00:00Z",
                          "2026-10-06T05:00:00Z",
                          "2026-10-08T05:00:00Z",
                          "2026-10-10T05:00:00Z",
                          "2026-10-12T05:00:00Z"};
    const double kgs[] = {92.3, 92.0, 92.1, 91.9, 91.8, 91.6};
    for (int i = 0; i < 6; ++i) {
        body.push_back(weight(days[i], kgs[i]));
    }
    const json h = Now::Today::health_block(summary, sleeps, body);
    EXPECT_EQ(h["steps"], 6120);
    EXPECT_EQ(h["active_kcal"], 370);
    EXPECT_EQ(h["resting_bpm"], 58);
    EXPECT_EQ(h["sleep"]["minutes"], 412) << "the nap is skipped";
    EXPECT_EQ(h["sleep"]["stages"].size(), 2u);
    EXPECT_EQ(h["sleep_minutes"], 412);
    EXPECT_DOUBLE_EQ(h["weight_kg"].get<double>(), 91.6);
    EXPECT_DOUBLE_EQ(h["weight_change_7d"].get<double>(), -0.5) << "91.6 against 92.1 on 10-06, 6 days older";
}

TEST(NowToday, HealthBlockWithAShortWeightSeriesHasNoChange) {
    const json body = json::array({weight("2026-10-10T05:00:00Z", 91.8), weight("2026-10-12T05:00:00Z", 91.6)});
    const json h = Now::Today::health_block(json(), json::array(), body);
    EXPECT_DOUBLE_EQ(h["weight_kg"].get<double>(), 91.6);
    EXPECT_TRUE(h["weight_change_7d"].is_null());
    EXPECT_TRUE(h["sleep"].is_null());
    EXPECT_TRUE(h["steps"].is_null());
}

TEST(NowToday, HealthBlockIsNullWithNothing) {
    EXPECT_TRUE(Now::Today::health_block(json(), json::array(), json::array()).is_null());
    EXPECT_TRUE(Now::Today::health_block(json{{"steps", nullptr}}, json::array(), json::array()).is_null());
}

TEST(NowToday, SpendsBlockKeepsFiveWithNamesAndCurrencies) {
    json rows = json::array();
    rows.push_back({{"id", "a"},
                    {"name", "Groceries"},
                    {"merchant", "Magnum"},
                    {"amount", 5000},
                    {"final_amount", 5000},
                    {"currency", "KZT"}});
    rows.push_back({{"id", "b"},
                    {"name", ""},
                    {"merchant", "Starbucks"},
                    {"amount", 4},
                    {"final_amount", 4.5},
                    {"currency", "USD"}});
    rows.push_back(
        {{"id", "c"}, {"name", ""}, {"merchant", ""}, {"amount", 300}, {"final_amount", nullptr}, {"currency", "THB"}});
    for (int i = 0; i < 4; ++i) {
        rows.push_back({{"id", "x" + std::to_string(i)},
                        {"name", "More"},
                        {"merchant", ""},
                        {"amount", 1},
                        {"final_amount", 1},
                        {"currency", "KZT"}});
    }
    const json b = Now::Today::spends_block(rows);
    ASSERT_EQ(b.size(), 5u);
    EXPECT_EQ(b[0], (json{{"id", "a"}, {"name", "Groceries"}, {"amount", 5000.0}, {"currency", "KZT"}}));
    EXPECT_EQ(b[1]["name"], "Starbucks");
    EXPECT_DOUBLE_EQ(b[1]["amount"].get<double>(), 4.5) << "the final amount, with adjustments";
    EXPECT_EQ(b[1]["currency"], "USD");
    EXPECT_EQ(b[2]["name"], "Spend");
    EXPECT_DOUBLE_EQ(b[2]["amount"].get<double>(), 300);
}

TEST(NowToday, SleepOfTheLocalDayFollowsTheAccountOffset) {
    // A cn account (UTC+8): woke at 06:58 local on 10-12 = 22:58 UTC on 10-11.
    const json rows = json::array({sleep_row("2026-10-11T22:58:00Z", 412, false),
                                   sleep_row("2026-10-10T23:10:00Z", 380, false),
                                   sleep_row("2026-10-12T17:00:00Z", 30, false)});
    const json kept = Now::Today::sleep_on_local_day(rows, "2026-10-12", 8 * 3600);
    ASSERT_EQ(kept.size(), 1u);
    EXPECT_EQ(kept[0]["time_asleep_minutes"], 412);
    // The same rows read in UTC keep the evening one of 10-12 only.
    const json utc = Now::Today::sleep_on_local_day(rows, "2026-10-12", 0);
    ASSERT_EQ(utc.size(), 1u);
    EXPECT_EQ(utc[0]["time_asleep_minutes"], 30);
}

TEST(NowToday, TravelBlockCountsDownToTheNextTrip) {
    const json trip = {{"id", "t1"}, {"name", "Thailand"}, {"start_date", "2026-11-01"}};
    const json b = Now::Today::travel_block(trip, json::array(), "2026-10-13");
    ASSERT_TRUE(b.is_object());
    EXPECT_EQ(b["next_trip"]["days_to"], 19);
    EXPECT_EQ(b["near"].size(), 0U);
}

TEST(NowToday, TravelBlockListsRulesNearTheirThreshold) {
    const json counters = {
        {{"name", "Schengen"}, {"threshold_kind", "at_most"}, {"left", 6}, {"reached_on", "2026-10-18"}},
        {{"name", "Thailand"}, {"threshold_kind", "at_most"}, {"left", 40}, {"reached_on", nullptr}},
        {{"name", "KZ tax"}, {"threshold_kind", "over"}, {"left", 30}, {"reached_on", "2026-10-20"}},
        {{"name", "Far"}, {"threshold_kind", "over"}, {"left", 3}, {"reached_on", "2026-12-31"}},
    };
    const json b = Now::Today::travel_block(nullptr, counters, "2026-10-13");
    ASSERT_TRUE(b.is_object());
    EXPECT_TRUE(b["next_trip"].is_null());
    ASSERT_EQ(b["near"].size(), 2U) << "Schengen with 6 days left, the tax rule reached in 7 days";
    EXPECT_EQ(b["near"][0]["rule"], "Schengen");
    EXPECT_EQ(b["near"][0]["left"], 6);
    EXPECT_EQ(b["near"][1]["rule"], "KZ tax");
    EXPECT_EQ(b["near"][1]["left"], 7) << "days until the threshold day";
}

TEST(NowToday, AReachedOverRuleIsNotNear) {
    // Living at home with the tax rule: 365 days used, the threshold met long ago.
    const json counters = {{{"name", "KZ tax"}, {"threshold_kind", "over"}, {"left", 0}, {"reached_on", "2026-10-13"}}};
    EXPECT_TRUE(Now::Today::travel_block(nullptr, counters, "2026-10-13").is_null());
}

TEST(NowToday, TravelBlockIsNullWhenThereIsNothing) {
    EXPECT_TRUE(Now::Today::travel_block(nullptr, json::array(), "2026-10-13").is_null());
}
