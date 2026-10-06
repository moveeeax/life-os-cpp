/**
 * @file test_food_parse_answer.cpp
 * @brief The answer schema of the food parse.
 */

#include <set>
#include <string>

#include <gtest/gtest.h>

#include "food/ParseAnswer.hpp"

namespace {

const std::set<std::string> kOwn = {"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa6"};
const char* const kExample =
    R"({"lines": [{"name": "Tonkatsu curry, small", "grams": 300, "kcal": 620, "protein_g": 22, "fat_g": 24,)"
    R"( "carbs_g": 78, "item_id": null, "estimated": true, "note": "size S"}]})";

}  // namespace

TEST(FoodParseAnswer, AcceptsTheExampleOfTheSpec) {
    const auto lines = Food::Parse::parse_answer(kExample, kOwn);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].name, "Tonkatsu curry, small");
    EXPECT_DOUBLE_EQ(lines[0].grams, 300);
    EXPECT_DOUBLE_EQ(lines[0].kcal, 620);
    EXPECT_DOUBLE_EQ(lines[0].carbs_g, 78);
    EXPECT_FALSE(lines[0].item_id.has_value());
    EXPECT_TRUE(lines[0].estimated);
    EXPECT_EQ(lines[0].note, "size S");
    const auto json = Food::Parse::to_json(lines);
    EXPECT_TRUE(json[0]["item_id"].is_null());
    EXPECT_EQ(json[0]["kcal"], 620);
}

TEST(FoodParseAnswer, StripsACodeFence) {
    const std::string fenced = std::string("```json\n") + kExample + "\n```";
    EXPECT_EQ(Food::Parse::parse_answer(fenced, kOwn).size(), 1u);
    EXPECT_EQ(Food::Parse::parse_answer(std::string("  \n") + kExample + "\n", kOwn).size(), 1u);
}

TEST(FoodParseAnswer, OwnItemIsAcceptedAndForeignIsNot) {
    const auto own = Food::Parse::parse_answer(
        R"({"lines":[{"name":"Egg","grams":60,"kcal":93,"item_id":"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa6","estimated":false}]})",
        kOwn);
    EXPECT_EQ(own[0].item_id.value(), "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa6");
    EXPECT_FALSE(own[0].estimated);
    EXPECT_THROW(
        Food::Parse::parse_answer(
            R"({"lines":[{"name":"Egg","grams":60,"kcal":93,"item_id":"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb6"}]})",
            kOwn),
        Food::Parse::Invalid);
    EXPECT_THROW(Food::Parse::parse_answer(R"({"lines":[{"name":"Egg","grams":60,"kcal":93,"item_id":"egg"}]})", kOwn),
                 Food::Parse::Invalid);
}

TEST(FoodParseAnswer, MissingMacrosAreZeroAndEstimatedDefaultsTrue) {
    const auto lines = Food::Parse::parse_answer(R"({"lines":[{"name":"Tea","grams":250,"kcal":2}]})", kOwn);
    EXPECT_DOUBLE_EQ(lines[0].protein_g, 0);
    EXPECT_TRUE(lines[0].estimated);
    EXPECT_EQ(lines[0].note, "");
}

TEST(FoodParseAnswer, RejectsBadNumbersAndShapes) {
    const auto bad = [&](const char* body) {
        EXPECT_THROW(Food::Parse::parse_answer(body, kOwn), Food::Parse::Invalid) << body;
    };
    bad("not json");
    bad(R"([1,2])");
    bad(R"({"items":[]})");
    bad(R"({"lines":[]})");
    bad(R"({"lines":[{"name":"x","grams":"300","kcal":1}]})");
    bad(R"({"lines":[{"name":"x","grams":0,"kcal":1}]})");
    bad(R"({"lines":[{"name":"x","grams":10,"kcal":-5}]})");
    bad(R"({"lines":[{"name":"x","grams":10}]})");
    bad(R"({"lines":[{"grams":10,"kcal":5}]})");
    bad(R"({"lines":[{"name":"","grams":10,"kcal":5}]})");
    bad(R"({"lines":[{"name":"x","grams":10,"kcal":5,"protein_g":5000}]})");
    bad(R"({"lines":["x"]})");
}

TEST(FoodParseAnswer, RejectsTooManyLinesAndCutsTheNote) {
    std::string many = R"({"lines":[)";
    for (int i = 0; i < 51; ++i) {
        many += std::string(i ? "," : "") + R"({"name":"x","grams":10,"kcal":5})";
    }
    many += "]}";
    EXPECT_THROW(Food::Parse::parse_answer(many, kOwn), Food::Parse::Invalid);

    const std::string long_note(600, 'n');
    const auto lines = Food::Parse::parse_answer(
        R"({"lines":[{"name":"x","grams":10,"kcal":5,"note":")" + long_note + R"("}]})", kOwn);
    EXPECT_EQ(lines[0].note.size(), Food::Parse::kNoteMax);
    EXPECT_EQ(lines[0].name.size(), 1u);
}

TEST(FoodParseAnswer, NamesAndNotesAreCutByCharactersNotBytes) {
    // 130 Cyrillic letters: 260 bytes, more than kNameMax characters.
    std::string name;
    for (int i = 0; i < 130; ++i) {
        name += "\xD0\xB0";  // "а"
    }
    std::string note;
    for (int i = 0; i < 600; ++i) {
        note += "\xD1\x8F";  // "я"
    }
    const nlohmann::json answer{{"lines", {{{"name", name}, {"grams", 100}, {"kcal", 10}, {"note", note}}}}};
    const auto lines = Food::Parse::parse_answer(answer.dump(), {});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].name.size(), Food::Parse::kNameMax * 2) << "cut, not rejected, on a character boundary";
    EXPECT_EQ(lines[0].note.size(), Food::Parse::kNoteMax * 2);
    EXPECT_NO_THROW(Food::Parse::to_json(lines).dump()) << "valid UTF-8 after the cut";
}
