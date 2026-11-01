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

TEST(Utf8, ClipAddsTheEllipsisOnlyWhenItCuts) {
    const std::string word = "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";  // "привет", 6 points, 12 bytes
    EXPECT_EQ(Utils::Utf8::clip(word, 6), word) << "exactly the limit is not cut";
    EXPECT_EQ(Utils::Utf8::clip(word, 99), word);
    EXPECT_EQ(Utils::Utf8::clip(word, 3), "\xD0\xBF\xD1\x80\xD0\xB8\xE2\x80\xA6") << "three letters and an ellipsis";
    EXPECT_EQ(Utils::Utf8::clip(word, 3, "..."), "\xD0\xBF\xD1\x80\xD0\xB8...");
    EXPECT_EQ(Utils::Utf8::clip("", 0), "");
}

TEST(Utf8, A200PointCutOfMixedTextKeepsEveryLetter) {
    // 151 ASCII letters, then 100 Cyrillic ones of two bytes each: a 200-byte cut ends on
    // the first byte of the 25th Cyrillic letter (the focus label bug); a cut by points
    // keeps 151 + 49 whole letters.
    std::string label(151, 'a');
    for (int i = 0; i < 100; ++i) {
        label += "\xD0\xB6";  // "ж"
    }
    EXPECT_NE(static_cast<unsigned char>(label[199]) & 0xC0, 0x80u) << "byte 200 is a lead byte: a byte cut splits";
    const std::string out = Utils::Utf8::cut(label, 200);
    EXPECT_EQ(Utils::Utf8::length(out), 200u);
    EXPECT_EQ(out.size(), 151u + 2u * 49u);
    EXPECT_EQ(static_cast<unsigned char>(out.back()), 0xB6u) << "ends on a whole letter";
}
