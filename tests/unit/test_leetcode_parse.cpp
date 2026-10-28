/**
 * @file test_leetcode_parse.cpp
 * @brief LeetCode::parse_stats: the solved counts from leetcode.com/graphql's
 *        public profile answer; LeetCode::valid_username.
 */

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "leetcode/Sync.hpp"

TEST(LeetCodeParse, SolvedCounts) {
    const std::string body = R"({"data":{"matchedUser":{"username":"moveeeax","submitStatsGlobal":{"acSubmissionNum":[)"
                             R"({"difficulty":"All","count":807},{"difficulty":"Easy","count":208},)"
                             R"({"difficulty":"Medium","count":451},{"difficulty":"Hard","count":148}]}}}})";
    const auto s = LeetCode::parse_stats(body);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->total, 807);
    EXPECT_EQ(s->easy, 208);
    EXPECT_EQ(s->medium, 451);
    EXPECT_EQ(s->hard, 148);
}

TEST(LeetCodeParse, UnknownUserOrJunk) {
    EXPECT_FALSE(LeetCode::parse_stats(R"({"data":{"matchedUser":null}})").has_value());
    EXPECT_FALSE(LeetCode::parse_stats("<html>rate limited</html>").has_value());
    EXPECT_FALSE(LeetCode::parse_stats(R"({"errors":[{"message":"x"}]})").has_value());
}

TEST(LeetCodeParse, Usernames) {
    EXPECT_TRUE(LeetCode::valid_username("moveeeax"));
    EXPECT_TRUE(LeetCode::valid_username("a_b-c.9"));
    EXPECT_FALSE(LeetCode::valid_username(""));
    EXPECT_FALSE(LeetCode::valid_username("bad name"));
    EXPECT_FALSE(LeetCode::valid_username(std::string(40, 'a')));
    EXPECT_FALSE(LeetCode::valid_username("x\"}"));
}
