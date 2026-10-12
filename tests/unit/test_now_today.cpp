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
    EXPECT_EQ(b["today"][0], (json{{"id", "c"}, {"title", "Call the bank"}}));
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
              (json{{"id", "4"}, {"title", "Weight"}, {"kind", "number"}, {"due", "2026-11-15"}, {"pace", "behind"}}));
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
