/**
 * @file test_notes_slug.cpp
 * @brief The slug of a note made from its title: ASCII kept lower-case, Cyrillic
 *        transliterated, other characters as one dash, cut at a word, and a free
 *        variant (my-note-2, my-note-3) when the owner already has it.
 */

#include <set>
#include <string>

#include <gtest/gtest.h>

#include "notes/Slug.hpp"

TEST(NotesSlug, FromAnEnglishTitle) {
    EXPECT_EQ(Notes::slug_of("VPC design: transit gateway vs peering"), "vpc-design-transit-gateway-vs-peering");
    EXPECT_EQ(Notes::slug_of("  C++20 & Drogon!  "), "c-20-drogon");
}

TEST(NotesSlug, CyrillicIsTransliterated) {
    EXPECT_EQ(Notes::slug_of("Стиль разборов и шаблон"), "stil-razborov-i-shablon");
    EXPECT_EQ(Notes::slug_of("Ёлка, щи и Юг"), "yolka-shchi-i-yug");
}

TEST(NotesSlug, NothingUsableGivesNote) {
    EXPECT_EQ(Notes::slug_of("!!!"), "note");
    EXPECT_EQ(Notes::slug_of("日本語"), "note");
}

TEST(NotesSlug, CutAtAWordWithin120) {
    std::string title;
    for (int i = 0; i < 40; ++i) {
        title += "word ";
    }
    const auto s = Notes::slug_of(title);
    EXPECT_LE(s.size(), 120U);
    EXPECT_NE(s.back(), '-');
    EXPECT_EQ(s.substr(s.size() - 4), "word");
}

TEST(NotesSlug, AFreeVariant) {
    EXPECT_EQ(Notes::free_slug("my-note", {}), "my-note");
    EXPECT_EQ(Notes::free_slug("my-note", {"my-note"}), "my-note-2");
    EXPECT_EQ(Notes::free_slug("my-note", {"my-note", "my-note-2"}), "my-note-3");
}
