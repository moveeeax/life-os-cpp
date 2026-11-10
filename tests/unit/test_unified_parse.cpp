/**
 * @file test_unified_parse.cpp
 * @brief The pure half of the unified parse (parse/Unified.hpp): the prompt
 *        made of the module prompts, the meal by the hour, and the answer
 *        split into parts each checked by its module's rules.
 */

#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "parse/Unified.hpp"

using json = nlohmann::json;

namespace {

UnifiedParse::Setup all_kinds() {
    UnifiedParse::Setup s;
    s.food = s.money = s.tasks = true;
    return s;
}

}  // namespace

TEST(UnifiedParsePrompt, WrapsTheModulePromptsAndNamesOnlyTheirKeys) {
    const std::string all = UnifiedParse::compose_prompt({"FOOD RULES", "MONEY RULES", "TASKS RULES"});
    EXPECT_NE(all.find("{\"food\": null, \"money\": null, \"tasks\": null, \"water\": null}"), std::string::npos);
    EXPECT_NE(all.find("## food\n\nFOOD RULES"), std::string::npos);
    EXPECT_NE(all.find("## money\n\nMONEY RULES"), std::string::npos);
    EXPECT_NE(all.find("## tasks\n\nTASKS RULES"), std::string::npos);

    UnifiedParse::Prompts money_only;
    money_only.money = "MONEY RULES";
    const std::string one = UnifiedParse::compose_prompt(money_only);
    EXPECT_NE(one.find("{\"money\": null}"), std::string::npos);
    EXPECT_EQ(one.find("## food"), std::string::npos);
    EXPECT_EQ(one.find("\"water\" is"), std::string::npos) << "water comes with the food module";
}

TEST(UnifiedParsePrompt, TheMealFollowsTheHour) {
    EXPECT_EQ(UnifiedParse::meal_at(7), "breakfast");
    EXPECT_EQ(UnifiedParse::meal_at(13), "lunch");
    EXPECT_EQ(UnifiedParse::meal_at(19), "dinner");
    EXPECT_EQ(UnifiedParse::meal_at(23), "snack");
    EXPECT_EQ(UnifiedParse::meal_at(2), "snack");
}

TEST(UnifiedParseAnswer, EachPartIsCheckedByItsModule) {
    UnifiedParse::Known known;
    known.date = "2026-10-07";
    const json answer{{"food", {{"lines", {{{"name", "Творог"}, {"grams", 200}, {"kcal", 242}}}}}},
                      {"money", {{"lines", {{{"amount", 1200}, {"name", "Кофе"}}}}}},
                      {"tasks", {{"lines", {{{"title", "Позвонить маме"}, {"area", "relationships"}}}}}},
                      {"water", {{"ml", 250}}}};
    const auto parts = UnifiedParse::parse_answer("```json\n" + answer.dump() + "\n```", all_kinds(), known);
    ASSERT_EQ(parts.food.size(), 1u);
    ASSERT_EQ(parts.money.size(), 1u);
    EXPECT_EQ(parts.money[0].date, "2026-10-07") << "a spend without a date takes the job's";
    ASSERT_EQ(parts.tasks.size(), 1u);
    EXPECT_EQ(parts.water_ml.value_or(0), 250);
    EXPECT_TRUE(parts.errors.empty());
}

TEST(UnifiedParseAnswer, NullAndEmptyPartsHoldNothing) {
    const json answer{{"food", nullptr}, {"money", {{"lines", json::array()}}}, {"water", nullptr}};
    const auto parts = UnifiedParse::parse_answer(answer.dump(), all_kinds(), {});
    EXPECT_TRUE(parts.empty());
    EXPECT_TRUE(parts.errors.empty()) << "a phrase with nothing to file is not an error";
}

TEST(UnifiedParseAnswer, ABrokenPartIsDroppedUnlessItIsAllThereIs) {
    UnifiedParse::Known known;
    known.date = "2026-10-07";
    const json answer{{"money", {{"lines", {{{"amount", 1200}, {"name", "Кофе"}}}}}},
                      {"tasks", {{"lines", {{{"title", "Deploy"}, {"area", "work"}}}}}},
                      {"water", {{"ml", 90000}}}};
    const auto parts = UnifiedParse::parse_answer(answer.dump(), all_kinds(), known);
    EXPECT_EQ(parts.money.size(), 1u);
    EXPECT_TRUE(parts.tasks.empty());
    EXPECT_TRUE(parts.errors.contains("tasks"));
    EXPECT_TRUE(parts.errors.contains("water"));
    EXPECT_FALSE(parts.water_ml.has_value());

    EXPECT_THROW(UnifiedParse::parse_answer(json{{"tasks", answer["tasks"]}}.dump(), all_kinds(), known),
                 UnifiedParse::Invalid);
    EXPECT_THROW(UnifiedParse::parse_answer("not json", all_kinds(), known), UnifiedParse::Invalid);
}

TEST(UnifiedParseAnswer, AKindTheSetupLeavesOutIsIgnored) {
    UnifiedParse::Setup money_only;
    money_only.money = true;
    const json answer{{"money", {{"lines", {{{"amount", 1200}, {"name", "Кофе"}}}}}},
                      {"tasks", {{"lines", {{{"title", "Позвонить маме"}, {"area", "relationships"}}}}}},
                      {"water", {{"ml", 250}}}};
    UnifiedParse::Known known;
    known.date = "2026-10-07";
    const auto parts = UnifiedParse::parse_answer(answer.dump(), money_only, known);
    EXPECT_EQ(parts.money.size(), 1u);
    EXPECT_TRUE(parts.tasks.empty());
    EXPECT_FALSE(parts.water_ml.has_value());
}
