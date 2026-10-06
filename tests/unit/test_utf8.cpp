#include <gtest/gtest.h>

#include "utils/Utf8.hpp"

TEST(Utf8, LengthCountsCodePoints) {
    EXPECT_EQ(Utils::Utf8::length(""), 0u);
    EXPECT_EQ(Utils::Utf8::length("abc"), 3u);
    EXPECT_EQ(Utils::Utf8::length("\xD0\xB0\xD0\xB1"), 2u);               // "аб"
    EXPECT_EQ(Utils::Utf8::length("a\xE2\x82\xAC\xF0\x9F\x8D\x8E"), 3u);  // "a€🍎"
}

TEST(Utf8, CutNeverSplitsACharacter) {
    const std::string mixed = "a\xD0\xB0\xE2\x82\xAC\xF0\x9F\x8D\x8Ez";  // "aа€🍎z"
    EXPECT_EQ(Utils::Utf8::cut(mixed, 0), "");
    EXPECT_EQ(Utils::Utf8::cut(mixed, 1), "a");
    EXPECT_EQ(Utils::Utf8::cut(mixed, 2), "a\xD0\xB0");
    EXPECT_EQ(Utils::Utf8::cut(mixed, 3), "a\xD0\xB0\xE2\x82\xAC");
    EXPECT_EQ(Utils::Utf8::cut(mixed, 4), "a\xD0\xB0\xE2\x82\xAC\xF0\x9F\x8D\x8E");
    EXPECT_EQ(Utils::Utf8::cut(mixed, 5), mixed);
    EXPECT_EQ(Utils::Utf8::cut(mixed, 99), mixed);
}

TEST(Utf8, CutSurvivesATruncatedTail) {
    // A lead byte that promises three bytes but the text ends: nothing past the end.
    EXPECT_EQ(Utils::Utf8::cut("ab\xE2\x82", 5), "ab\xE2\x82");
}
