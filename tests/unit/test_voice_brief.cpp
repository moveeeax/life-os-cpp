/**
 * @file test_voice_brief.cpp
 * @brief Ani's voice line to Telegram: when it is due and what it says.
 */

#include <gtest/gtest.h>

#include "agent/VoiceBrief.hpp"

namespace B = Agent::VoiceBrief;

TEST(VoiceBrief, DueAtEightAndAtNine) {
    EXPECT_EQ(B::moment_of("2026-10-31 08:05, Sat"), "morning");
    EXPECT_EQ(B::moment_of("2026-10-31 21:40, Sat"), "evening");
    EXPECT_FALSE(B::moment_of("2026-10-31 09:00, Sat"));
    EXPECT_FALSE(B::moment_of(""));
}

TEST(VoiceBrief, WordsFromTheDay) {
    EXPECT_EQ(B::brief_words("morning", {3, 1, 0, 5, 0}),
              "Доброе утро! Задач на сегодня: 3. Просроченных: 1. Привычек на день: 5. Я рядом, если что.");
    EXPECT_EQ(B::brief_words("morning", {0, 0, 0, 0, 0}), "Доброе утро! На сегодня задач нет. Я рядом, если что.");
    EXPECT_EQ(B::brief_words("evening", {1, 0, 4, 5, 3}),
              "Вечер. Сегодня закрыто задач: 4. Привычки: 3 из 5. На завтра осталось: 1. Спокойной ночи.");
}
