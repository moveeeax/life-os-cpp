#include <map>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "money/ParseAnswer.hpp"

namespace {

const std::string kKaspi = "11111111-1111-4111-8111-111111111111";
const std::string kFood = "22222222-2222-4222-8222-222222222222";
const std::string kSalary = "33333333-3333-4333-8333-333333333333";
const std::string kForeign = "99999999-9999-4999-8999-999999999999";
const std::set<std::string> kAccounts{kKaspi};
const std::map<std::string, std::string> kCategories{{kFood, "expense"}, {kSalary, "income"}};

std::vector<Money::Parse::Line> parse(const nlohmann::json& answer) {
    return Money::Parse::parse_answer(answer.dump(), "2026-10-07", kAccounts, kCategories);
}

}  // namespace

TEST(MoneyParseAnswer, AcceptsTheExampleWithMatchedIds) {
    const auto lines = parse({{"lines",
                               {{{"type", "expense"},
                                 {"date", "2026-10-05"},
                                 {"time", "12:41"},
                                 {"account_id", kKaspi},
                                 {"amount", 13275.61},
                                 {"merchant", "BIG C PHUKET"},
                                 {"name", "Groceries (store, THB)"},
                                 {"category_id", kFood},
                                 {"receipt_amount", 955.75},
                                 {"receipt_currency", "THB"},
                                 {"fx_note", "THB -> KZT"},
                                 {"confidence", 0.9}}}}});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].account_id, kKaspi);
    EXPECT_EQ(lines[0].category_id, kFood);
    EXPECT_EQ(*lines[0].time, "12:41");
    EXPECT_DOUBLE_EQ(*lines[0].receipt_amount, 955.75);
    EXPECT_EQ(lines[0].note, "");
    EXPECT_EQ(Money::Parse::to_json(lines)[0]["receipt_currency"], "THB");
}

TEST(MoneyParseAnswer, ForeignOrMismatchedIdsBecomeNullWithANote) {
    const auto lines = parse({{"lines",
                               {{{"account_id", kForeign}, {"amount", 10}, {"name", "a"}, {"category_id", kForeign}},
                                {{"type", "expense"}, {"amount", 10}, {"name", "b"}, {"category_id", kSalary}}}}});
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_FALSE(lines[0].account_id.has_value());
    EXPECT_FALSE(lines[0].category_id.has_value());
    EXPECT_EQ(lines[0].note, "account not matched, pick one; category not matched, pick one");
    EXPECT_FALSE(lines[1].category_id.has_value()) << "an income category on an expense line";
}

TEST(MoneyParseAnswer, DefaultsAndAFence) {
    const auto lines = Money::Parse::parse_answer(
        "```json\n{\"lines\":[{\"amount\":5,\"merchant\":\"7-Eleven\"}]}\n```", "2026-10-07", kAccounts, kCategories);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].type, "expense");
    EXPECT_EQ(lines[0].date, "2026-10-07") << "the hint date";
    EXPECT_EQ(lines[0].name, "7-Eleven") << "the merchant stands in for a missing name";
    EXPECT_FALSE(lines[0].time.has_value());
}

TEST(MoneyParseAnswer, RejectsWhatCannotBeALine) {
    EXPECT_THROW(parse({{"lines", nlohmann::json::array()}}), Money::Parse::Invalid);
    EXPECT_THROW(parse({{"rows", {{{"amount", 1}}}}}), Money::Parse::Invalid);
    EXPECT_THROW(parse({{"lines", {{{"amount", -1}, {"name", "x"}}}}}), Money::Parse::Invalid);
    EXPECT_THROW(parse({{"lines", {{{"amount", "1"}, {"name", "x"}}}}}), Money::Parse::Invalid);
    EXPECT_THROW(parse({{"lines", {{{"amount", 1}, {"name", "x"}, {"date", "2026-02-30"}}}}}), Money::Parse::Invalid);
    EXPECT_THROW(parse({{"lines", {{{"amount", 1}, {"name", "x"}, {"type", "transfer"}}}}}), Money::Parse::Invalid);
    EXPECT_THROW(parse({{"lines", {{{"amount", 1}}}}}), Money::Parse::Invalid) << "no name and no merchant";
    nlohmann::json many = nlohmann::json::array();
    for (int i = 0; i < 101; ++i) {
        many.push_back({{"amount", 1}, {"name", "x"}});
    }
    EXPECT_THROW(parse({{"lines", many}}), Money::Parse::Invalid);
    EXPECT_THROW(Money::Parse::parse_answer("not json", "2026-10-07", kAccounts, kCategories), Money::Parse::Invalid);
}

TEST(MoneyParseAnswer, AHalfReceiptPairIsDroppedAndNamesAreCutByCharacters) {
    std::string name;
    for (int i = 0; i < 250; ++i) {
        name += "\xD0\xB0";  // "а"
    }
    const auto lines = parse({{"lines", {{{"amount", 1}, {"name", name}, {"receipt_amount", 5}}}}});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_FALSE(lines[0].receipt_amount.has_value()) << "an amount without its currency";
    EXPECT_EQ(lines[0].name.size(), Money::Parse::kNameMax * 2);
}
