#include <gtest/gtest.h>

#include "tasks/ParseAnswer.hpp"

using Tasks::Parse::Invalid;
using Tasks::Parse::parse_answer;

TEST(TasksParseAnswer, TakesSeveralLinesAndFlagsAnOpenDuplicate) {
    const auto lines = parse_answer(
        R"({"lines": [
            {"title": "Купить воду", "area": "projects", "effort": "5min", "due": "2026-10-08", "next_step": "", "confidence": 0.9},
            {"title": "Позвонить маме", "area": "relationships", "effort": null, "due": null, "next_step": "Вечером", "confidence": 0.8}
        ]})",
        {"позвонить маме"});
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0].due, "2026-10-08");
    EXPECT_FALSE(lines[0].possible_duplicate);
    EXPECT_FALSE(lines[1].effort.has_value());
    EXPECT_TRUE(lines[1].possible_duplicate) << "the same title is open already";
}

TEST(TasksParseAnswer, RefusesWhatTheRulesDoNotAllow) {
    EXPECT_THROW(parse_answer(R"({"lines": []})", {}), Invalid) << "no lines";
    EXPECT_THROW(parse_answer(R"({"lines": [{"title": "x", "area": "work"}]})", {}), Invalid) << "work is not an area";
    EXPECT_THROW(parse_answer(R"({"lines": [{"title": "", "area": "health"}]})", {}), Invalid) << "empty title";
    EXPECT_THROW(parse_answer(R"({"lines": [{"title": "x", "area": "health", "due": "2026-02-30"}]})", {}), Invalid);
    EXPECT_THROW(parse_answer("not json", {}), Invalid);
}

TEST(TasksParseAnswer, DropsAnUnknownEffortAndCutsLongText) {
    const auto lines = parse_answer(R"({"lines": [{"title": "x", "area": "growth", "effort": "1h", "next_step": ")" +
                                        std::string(400, 'a') + R"("}]})",
                                    {});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_FALSE(lines[0].effort.has_value()) << "an effort outside the three is dropped, not fatal";
    EXPECT_EQ(lines[0].next_step.size(), 300u);
}

TEST(TasksParseAnswer, ReadsAFencedAnswer) {
    const auto lines = parse_answer("```json\n{\"lines\": [{\"title\": \"x\", \"area\": \"finance\"}]}\n```", {});
    ASSERT_EQ(lines.size(), 1u);
}
