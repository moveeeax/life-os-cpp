/**
 * @file test_fitness_csv_escape.cpp
 * @brief CSV escaping: formulas are neutralized like in the bridge, numbers stay intact.
 *
 * Phase 3 review, Important 1-2: a leading space/tab/CR bypassed the formula
 * protection, and the apostrophe stuck to every value, including negative
 * numbers. Bridge rules (_escape_csv_value): lstrip before the check,
 * prefixes = + - @ TAB CR, only string values are escaped.
 */

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "fitness/Export.hpp"

using nlohmann::json;

TEST(CsvEscape, LeadingWhitespaceDoesNotBypassTheFormulaGuard) {
    const json rows = json::array({{{"a", " =HYPERLINK(\"x\")"}, {"b", "\t=1+1"}, {"c", "\r@cmd"}}});
    const std::string csv = Fitness::Export::to_csv(rows);
    EXPECT_EQ(csv.find(", =HYPERLINK"), std::string::npos) << csv;
    EXPECT_NE(csv.find("' =HYPERLINK"), std::string::npos) << csv;
    EXPECT_NE(csv.find("'\t=1+1"), std::string::npos) << csv;
    EXPECT_NE(csv.find("'\r@cmd"), std::string::npos) << csv;
}

TEST(CsvEscape, NumbersAreNotMangled) {
    const json rows = json::array({{{"delta", -3.5}, {"n", -7}}});
    const std::string csv = Fitness::Export::to_csv(rows);
    EXPECT_EQ(csv.find("'-3.5"), std::string::npos) << csv;
    EXPECT_NE(csv.find("-3.5"), std::string::npos) << csv;
    EXPECT_NE(csv.find("-7"), std::string::npos) << csv;
}

TEST(CsvEscape, EmptyRowsStillCarryNothingButNoCrash) {
    EXPECT_EQ(Fitness::Export::to_csv(json::array()), "");
}

TEST(FitnessExport, EnvelopeMatchesBridgeSchema) {
    const json records = json{{"sleep", json::array()}};
    const json env = Fitness::Export::envelope(records, "sleep", "2026-09-01", "2026-09-02", 1790000000LL);
    EXPECT_EQ(env["schema_version"], "1.0");
    EXPECT_EQ(env["source"], "life-os-cpp");
    EXPECT_EQ(env["filters"]["dataset"], "sleep");
    EXPECT_EQ(env["filters"]["start_date"], "2026-09-01");
    EXPECT_EQ(env["filters"]["end_date"], "2026-09-02");
    EXPECT_TRUE(env["generated_at"].is_string());
    EXPECT_EQ(env["records"], records);
    const json all = Fitness::Export::envelope(records, "", "2026-09-01", "2026-09-02", 1790000000LL);
    EXPECT_TRUE(all["filters"]["dataset"].is_null());
}

TEST(FitnessExport, TypesListMatchesTheBridgeOrder) {
    const auto& t = Fitness::Export::types();
    ASSERT_EQ(t.size(), 8u);
    EXPECT_EQ(t.front(), "daily_activity");
    EXPECT_EQ(t.back(), "abnormal_heart_beat");
}
