/**
 * @file test_ani_cues.cpp
 * @brief Ani's cue rules (agent/Cues.hpp), one block per rule as the iOS app
 *        ran them (App/Data/AniCues.swift): a meeting soon without lunch, the
 *        latest meal due and not written, a short night, the tape, a budget
 *        overrun; the order of the five, the words, the JSON of a cue, the
 *        ids a dismissal accepts and an agent's action checked.
 */

#include <optional>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/Cues.hpp"

using json = nlohmann::json;
namespace C = Agent::Cues;

namespace {

constexpr int at(int h, int m) {
    return h * 60 + m;
}

/// A day with food read and nothing written, at @p minute.
C::Input day_at(int minute) {
    C::Input in;
    in.date = "2026-11-09";
    in.minute = minute;
    in.meals = std::set<std::string>();
    return in;
}

std::vector<std::string> ids(const std::vector<C::Cue>& cues) {
    std::vector<std::string> out;
    for (const auto& c : cues) {
        out.push_back(c.id);
    }
    return out;
}

const C::Cue* find(const std::vector<C::Cue>& cues, const std::string& id) {
    for (const auto& c : cues) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

const std::string kNarrow = "\xE2\x80\xAF";

}  // namespace

// ── 1. a meeting soon, lunch not written ────────────────────────────────────

TEST(AniCues, AMeetingSoonAtLunchTimeAsksForLunch) {
    auto in = day_at(at(12, 20));
    in.meetings = {-30, 10, 40, 55};
    const auto cues = C::rules(in);
    const auto* c = find(cues, "meeting-soon");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->screen, "today");
    EXPECT_EQ(c->text, "Через 40 минут созвон. Пообедаем до него?") << "the first meeting 15 to 60 minutes away";
    ASSERT_TRUE(c->action.has_value());
    EXPECT_EQ(c->action->label, "Обед");
    EXPECT_EQ(c->action->kind, "food");
    EXPECT_EQ(c->action->payload, (json{{"meal", "lunch"}}));
}

TEST(AniCues, AMeetingSoonCountsFifteenAndSixtyMinutes) {
    auto in = day_at(at(11, 0));
    in.meetings = {15};
    EXPECT_NE(find(C::rules(in), "meeting-soon"), nullptr);
    in.meetings = {60};
    EXPECT_NE(find(C::rules(in), "meeting-soon"), nullptr);
    in.meetings = {14, 61};
    EXPECT_EQ(find(C::rules(in), "meeting-soon"), nullptr);
}

TEST(AniCues, AMeetingSoonStaysQuietOutsideLunchTimeOrWithLunchWritten) {
    auto in = day_at(at(10, 59));
    in.meetings = {30};
    EXPECT_EQ(find(C::rules(in), "meeting-soon"), nullptr) << "before 11:00";
    in.minute = at(16, 0);
    EXPECT_EQ(find(C::rules(in), "meeting-soon"), nullptr) << "16:00 and after";
    in.minute = at(12, 0);
    in.meals = std::set<std::string>{"lunch"};
    EXPECT_EQ(find(C::rules(in), "meeting-soon"), nullptr) << "lunch is written";
    in.meals.reset();
    EXPECT_EQ(find(C::rules(in), "meeting-soon"), nullptr) << "food not read";
}

// ── 2. the latest meal due and not written ──────────────────────────────────

TEST(AniCues, TheLatestMealDueIsAskedFor) {
    auto in = day_at(at(13, 30));
    in.meals = std::set<std::string>{"breakfast"};
    const auto cues = C::rules(in);
    const auto* c = find(cues, "meal-missing-lunch");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->screen, "food");
    EXPECT_EQ(c->text, "Обед еще не записан. Запишем?");
    ASSERT_TRUE(c->action.has_value());
    EXPECT_EQ(c->action->label, "Обед");
    EXPECT_EQ(c->action->kind, "food");
    EXPECT_EQ(c->action->payload, (json{{"meal", "lunch"}}));

    in.minute = at(10, 30);
    in.meals = std::set<std::string>();
    EXPECT_NE(find(C::rules(in), "meal-missing-breakfast"), nullptr);
    EXPECT_EQ(find(C::rules(in), "meal-missing-breakfast")->text, "Завтрак еще не записан. Запишем?");
    in.minute = at(20, 0);
    EXPECT_EQ(find(C::rules(in), "meal-missing-dinner")->text, "Ужин еще не записан. Запишем?");
}

TEST(AniCues, AnEarlierMealSkippedIsNotBroughtUpOnceALaterIsIn) {
    auto in = day_at(at(21, 0));
    in.meals = std::set<std::string>{"dinner"};
    EXPECT_TRUE(C::rules(in).empty()) << "dinner is in: breakfast and lunch are left alone";
}

TEST(AniCues, NoMealIsDueBeforeHalfPastTen) {
    auto in = day_at(at(10, 29));
    EXPECT_TRUE(C::rules(in).empty());
    in.minute = C::kDayAhead;
    EXPECT_TRUE(C::rules(in).empty()) << "a day not begun asks for nothing";
    in.minute = C::kDayOver;
    EXPECT_EQ(ids(C::rules(in)), (std::vector<std::string>{"meal-missing-dinner"})) << "a past day: dinner";
}

// ── 3. a short night ─────────────────────────────────────────────────────────

TEST(AniCues, ANightUnderSixHoursIsMentioned) {
    C::Input in;
    in.date = "2026-11-09";
    in.sleep_minutes = 359;
    const auto cues = C::rules(in);
    ASSERT_EQ(cues.size(), 1u);
    EXPECT_EQ(cues[0].id, "sleep-short");
    EXPECT_EQ(cues[0].screen, "body");
    EXPECT_EQ(cues[0].text, "Сон 5 ч 59. Сегодня полегче: без тяжелых подходов?");
    EXPECT_FALSE(cues[0].action.has_value());

    in.sleep_minutes = 45;
    EXPECT_EQ(C::rules(in)[0].text, "Сон 45 мин. Сегодня полегче: без тяжелых подходов?");
    in.sleep_minutes = 360;
    EXPECT_TRUE(C::rules(in).empty());
    in.sleep_minutes.reset();
    EXPECT_TRUE(C::rules(in).empty()) << "no night known";
}

// ── 4. the tape ──────────────────────────────────────────────────────────────

TEST(AniCues, TheTapeIsAskedForAfterMoreThanTenDays) {
    C::Input in;
    in.date = "2026-11-09";
    in.body = true;
    in.last_measure = "2026-10-30";
    EXPECT_TRUE(C::rules(in).empty()) << "10 days: not yet";

    in.last_measure = "2026-10-28";
    auto cues = C::rules(in);
    ASSERT_EQ(cues.size(), 1u);
    EXPECT_EQ(cues[0].id, "measure-stale");
    EXPECT_EQ(cues[0].screen, "body");
    EXPECT_EQ(cues[0].text, "Талию не мерили 12 дней. Сделаем сегодня?");
    ASSERT_TRUE(cues[0].action.has_value());
    EXPECT_EQ(cues[0].action->label, "Замер");
    EXPECT_EQ(cues[0].action->kind, "measure");
    EXPECT_TRUE(cues[0].action->payload.is_null());

    in.last_measure = "2026-10-19";
    EXPECT_EQ(C::rules(in)[0].text, "Талию не мерили 21 день. Сделаем сегодня?");
    in.last_measure = "2026-10-18";
    EXPECT_EQ(C::rules(in)[0].text, "Талию не мерили 22 дня. Сделаем сегодня?");
}

TEST(AniCues, NoMeasurementYetAsksForTheFirst) {
    C::Input in;
    in.date = "2026-11-09";
    in.body = true;
    const auto cues = C::rules(in);
    ASSERT_EQ(cues.size(), 1u);
    EXPECT_EQ(cues[0].id, "measure-stale");
    EXPECT_EQ(cues[0].text, "Замеров еще нет. Сделаем первый?");
    in.body = false;
    EXPECT_TRUE(C::rules(in).empty()) << "the body sources not read";
}

// ── 5. a category over its budget ───────────────────────────────────────────

TEST(AniCues, TheFirstCategoryOverItsBudgetInItsCurrency) {
    C::Input in;
    in.date = "2026-11-09";
    in.money = std::vector<C::Category>{
        {"Еда", "KZT", 61200, 90000},
        {"Жилье", "KZT", 80000, 80000},
        {"Здоровье", "KZT", 11800, std::nullopt},
        {"Кафе", "KZT", 23800.4, 20000},
        {"Подписки", "USD", 50, 40},
    };
    const auto cues = C::rules(in);
    ASSERT_EQ(cues.size(), 1u);
    EXPECT_EQ(cues[0].id, "budget-over");
    EXPECT_EQ(cues[0].screen, "money");
    EXPECT_EQ(cues[0].text, "«Кафе» уже на 3" + kNarrow + "800 ₸ выше бюджета.")
        << "equal to the budget is not over; no budget is never over";
    EXPECT_FALSE(cues[0].action.has_value());

    in.money = std::vector<C::Category>{{"Books", "CHF", 12.6, 10}};
    EXPECT_EQ(C::rules(in)[0].text, "«Books» уже на 3 CHF выше бюджета.") << "an unknown currency keeps its code";
    in.money = std::vector<C::Category>{};
    EXPECT_TRUE(C::rules(in).empty());
}

// ── the five together ───────────────────────────────────────────────────────

TEST(AniCues, TheRulesComeInTheClientsOrder) {
    auto in = day_at(at(13, 40));
    in.meetings = {30};
    in.sleep_minutes = 300;
    in.body = true;
    in.money = std::vector<C::Category>{{"Кафе", "KZT", 25000, 20000}};
    EXPECT_EQ(ids(C::rules(in)),
              (std::vector<std::string>{
                  "meeting-soon", "meal-missing-lunch", "sleep-short", "measure-stale", "budget-over"}));
    for (const auto& id : ids(C::rules(in))) {
        EXPECT_TRUE(C::is_rule_id(id)) << id;
    }
}

// ── the words ────────────────────────────────────────────────────────────────

TEST(AniCues, TheWordsAgreeWithTheirNumbers) {
    EXPECT_EQ(C::plural(1, "день", "дня", "дней"), "1 день");
    EXPECT_EQ(C::plural(3, "день", "дня", "дней"), "3 дня");
    EXPECT_EQ(C::plural(5, "день", "дня", "дней"), "5 дней");
    EXPECT_EQ(C::plural(11, "день", "дня", "дней"), "11 дней");
    EXPECT_EQ(C::plural(12, "день", "дня", "дней"), "12 дней");
    EXPECT_EQ(C::plural(101, "день", "дня", "дней"), "101 день");
    EXPECT_EQ(C::duration(432), "7 ч 12");
    EXPECT_EQ(C::duration(420), "7 ч 00");
    EXPECT_EQ(C::duration(45), "45 мин");
    EXPECT_EQ(C::duration(-5), "0 мин");
    EXPECT_EQ(C::thousands(0), "0");
    EXPECT_EQ(C::thousands(999), "999");
    EXPECT_EQ(C::thousands(3800), "3" + kNarrow + "800");
    EXPECT_EQ(C::thousands(12345), "12" + kNarrow + "345");
    EXPECT_EQ(C::thousands(1234567), "1" + kNarrow + "234" + kNarrow + "567");
    EXPECT_EQ(C::currency_sign("KZT"), "₸");
    EXPECT_EQ(C::currency_sign("USD"), "$");
    EXPECT_EQ(C::currency_sign("XYZ"), "XYZ");
}

// ── the JSON, the ids, the agent's cue ──────────────────────────────────────

TEST(AniCues, ACueAsTheApiGivesIt) {
    const C::Cue with{
        "meal-missing-lunch", "food", "Обед еще не записан. Запишем?", C::Action{"Обед", "food", {{"meal", "lunch"}}}};
    const json a = C::to_json(with, "2026-11-09T19:00:00Z");
    EXPECT_EQ(a,
              (json{{"id", "meal-missing-lunch"},
                    {"screen", "food"},
                    {"text", "Обед еще не записан. Запишем?"},
                    {"action", {{"label", "Обед"}, {"kind", "food"}, {"payload", {{"meal", "lunch"}}}}},
                    {"expires_at", "2026-11-09T19:00:00Z"}}));
    const json b = C::to_json(C::Cue{"measure-stale", "body", "x", C::Action{"Замер", "measure", json()}}, "t");
    EXPECT_FALSE(b["action"].contains("payload")) << "no payload, no key";
    const json c = C::to_json(C::Cue{"sleep-short", "body", "x", std::nullopt}, "t");
    EXPECT_TRUE(c["action"].is_null());
}

TEST(AniCues, AnAgentRowBecomesACue) {
    const json row = {{"id", "0b9f4c3e-5d1a-4a7e-9c11-2f6f0d4b8a10"},
                      {"screen", "money"},
                      {"text", "Такси уже третий раз за день"},
                      {"action", {{"label", "Записать"}, {"kind", "spend"}}}};
    const auto c = C::from_row(row);
    EXPECT_EQ(c.id, "agent-0b9f4c3e-5d1a-4a7e-9c11-2f6f0d4b8a10");
    EXPECT_EQ(c.screen, "money");
    ASSERT_TRUE(c.action.has_value());
    EXPECT_EQ(c.action->kind, "spend");
    EXPECT_TRUE(c.action->payload.is_null());
    EXPECT_FALSE(
        C::from_row(json{{"id", "x"}, {"screen", "today"}, {"text", "t"}, {"action", nullptr}}).action.has_value());
}

TEST(AniCues, TheIdsADismissalTakes) {
    EXPECT_TRUE(C::is_rule_id("meeting-soon"));
    EXPECT_TRUE(C::is_rule_id("meal-missing-dinner"));
    EXPECT_FALSE(C::is_rule_id("meal-missing-snack")) << "no rule asks for the snack";
    EXPECT_FALSE(C::is_rule_id("plan-snack"));
    EXPECT_EQ(C::agent_uuid("agent-0b9f4c3e-5d1a-4a7e-9c11-2f6f0d4b8a10"),
              std::optional<std::string>("0b9f4c3e-5d1a-4a7e-9c11-2f6f0d4b8a10"));
    EXPECT_FALSE(C::agent_uuid("agent-nope").has_value());
    EXPECT_FALSE(C::agent_uuid("0b9f4c3e-5d1a-4a7e-9c11-2f6f0d4b8a10").has_value());
}

TEST(AniCues, AnAgentsActionIsChecked) {
    EXPECT_FALSE(C::action_problem(json()).has_value());
    EXPECT_FALSE(
        C::action_problem(json{{"label", "Да"}, {"kind", "food"}, {"payload", {{"meal", "snack"}}}}).has_value());
    EXPECT_TRUE(C::action_problem(json("food")).has_value());
    EXPECT_TRUE(C::action_problem(json{{"label", ""}, {"kind", "food"}}).has_value());
    EXPECT_TRUE(C::action_problem(json{{"label", std::string(31, 'a')}, {"kind", "food"}}).has_value());
    EXPECT_TRUE(C::action_problem(json{{"label", "Да"}, {"kind", "dance"}}).has_value());
    EXPECT_TRUE(C::action_problem(json{{"label", "Да"}, {"kind", "food"}, {"payload", "lunch"}}).has_value());
    EXPECT_TRUE(C::action_problem(json{{"label", "Да"}, {"kind", "food"}, {"payload", {{"x", std::string(1001, 'a')}}}})
                    .has_value());
    EXPECT_TRUE(C::action_problem(json{{"label", "Да"}, {"kind", "food"}, {"url", "https://x"}}).has_value());
}
