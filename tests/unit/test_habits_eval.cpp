/**
 * @file test_habits_eval.cpp
 * @brief The pure habit evaluation (src/habits/Habits.hpp): a day done by
 *        hand or by a catalog source meeting its goal at least or at most, a
 *        presence source without a goal, the sources a set of habits reads
 *        once each, the daily streak with an open today across a month and a
 *        year boundary, the Monday-to-Sunday week and its streak across New
 *        Year, the best streak, a weigh-in with and without a goal, the Now
 *        plate, a daily target on a habit ticked by hand, and the tags of a
 *        mood check-in. The worked numbers are in the comments; 2026-10-14 is
 *        a Wednesday, 2026-01-01 a Thursday.
 */

#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "habits/Habits.hpp"
#include "habits/Mood.hpp"
#include "metrics/Catalog.hpp"

using Habits::Day;
using Habits::Direction;
using Habits::Habit;
using Habits::Schedule;
using json = nlohmann::json;

namespace {

Day d(const char* text) {
    return Habits::day_of(text);
}

std::set<Day> days(std::initializer_list<const char*> list) {
    std::set<Day> out;
    for (const char* t : list) {
        out.insert(d(t));
    }
    return out;
}

/// A daily habit on the catalog id @p source (nullptr: ticked by hand).
Habit daily(const char* source = nullptr,
            std::optional<double> goal = std::nullopt,
            Direction direction = Direction::AtLeast) {
    Habit h;
    h.goal = goal;
    if (source != nullptr) {
        h.source = Metrics::parse(source);
        EXPECT_TRUE(h.source.has_value()) << source;
    }
    h.direction = direction;
    return h;
}

Habit weekly(int per_week) {
    Habit h;
    h.schedule = Schedule::Weekly;
    h.per_week = per_week;
    return h;
}

}  // namespace

// ── the day ─────────────────────────────────────────────────────────────────

TEST(HabitsEval, ParsesTheWordsOfTheModel) {
    EXPECT_TRUE(Habits::schedule_of("daily") == Schedule::Daily);
    EXPECT_TRUE(Habits::schedule_of("weekly") == Schedule::Weekly);
    EXPECT_FALSE(Habits::schedule_of("monthly").has_value());
    EXPECT_TRUE(Habits::direction_of("at_least") == Direction::AtLeast);
    EXPECT_TRUE(Habits::direction_of("at_most") == Direction::AtMost);
    EXPECT_FALSE(Habits::direction_of("exactly").has_value());
    EXPECT_FALSE(Habits::source_of("manual").has_value()) << "ticked by hand: no series";
    EXPECT_FALSE(Habits::source_of("water").has_value()) << "names nothing";
    ASSERT_TRUE(Habits::source_of("food:kcal").has_value());
    EXPECT_EQ(Metrics::canonical(*Habits::source_of("food:kcal")), "food:kcal");
}

// The five sources of 1.33 read as their catalog ids, stored rows and API input alike.
TEST(HabitsEval, LegacySourceNamesAreAccepted) {
    const std::vector<std::pair<const char*, const char*>> legacy{{"sleep", "fitness:sleep"},
                                                                  {"steps", "fitness:steps"},
                                                                  {"active_kcal", "fitness:active_kcal"},
                                                                  {"workout", "workout:sessions"},
                                                                  {"weight", "fitness:weight"}};
    for (const auto& [old, now] : legacy) {
        const auto ref = Habits::source_of(old);
        ASSERT_TRUE(ref.has_value()) << old;
        EXPECT_EQ(Metrics::canonical(*ref), now) << old;
    }
}

// Sleep goal 8 h (the catalog gives hours); steps 8000; active kcal 500; sessions
// counted, presence: any session without a goal, at least the goal with one.
TEST(HabitsEval, EachSourceAgainstItsGoal) {
    EXPECT_TRUE(Habits::meets(daily("fitness:sleep", 8), 8.0));
    EXPECT_FALSE(Habits::meets(daily("fitness:sleep", 8), 7.98)) << "7 h 59 m is short of 8 h";
    EXPECT_TRUE(Habits::meets(daily("fitness:sleep", 7.5), 7.5));
    EXPECT_TRUE(Habits::meets(daily("fitness:steps", 8000), 8000));
    EXPECT_FALSE(Habits::meets(daily("fitness:steps", 8000), 7999));
    EXPECT_TRUE(Habits::meets(daily("fitness:active_kcal", 500), 512.5));
    EXPECT_FALSE(Habits::meets(daily("fitness:active_kcal", 500), 499.9));
    EXPECT_TRUE(Habits::meets(daily("workout:sessions"), 1)) << "one finished session";
    EXPECT_FALSE(Habits::meets(daily("workout:sessions", 2), 1)) << "two a day asked, one done";
    EXPECT_TRUE(Habits::meets(daily("workout:sessions", 2), 2));
    EXPECT_FALSE(Habits::meets(daily(nullptr, 45), 1000)) << "a manual habit is never done by data";
    EXPECT_FALSE(Habits::meets(daily("fitness:steps"), 100000)) << "a sum without a goal is never met";
}

// food:kcal at most 2200: 2100 and 2200 done, 2300 not.
TEST(HabitsEval, AtMostDirection) {
    const Habit kcal = daily("food:kcal", 2200, Direction::AtMost);
    EXPECT_TRUE(Habits::meets(kcal, 2100));
    EXPECT_TRUE(Habits::meets(kcal, 2200)) << "the goal itself is within";
    EXPECT_FALSE(Habits::meets(kcal, 2300));
    const Habits::Values eaten{{d("2026-10-12"), 2100}, {d("2026-10-13"), 2300}};
    EXPECT_EQ(Habits::done_days(kcal, {}, eaten), days({"2026-10-12"}))
        << "a day without entries is not done: nothing was logged";
    EXPECT_FALSE(Habits::day_state(kcal, d("2026-10-14"), {}, eaten).done);
}

// A ledger day without a row is 0 for an at-most goal: money, practice, tasks. Food is not a
// ledger (nothing logged is not kept), at-least goals and the balance (a state) stay as they are.
TEST(HabitsEval, AnAtMostLedgerDayWithoutARowIsZero) {
    EXPECT_TRUE(Habits::empty_day_is_zero(daily("money:expense:KZT", 5000, Direction::AtMost)));
    EXPECT_TRUE(Habits::empty_day_is_zero(
        daily("money:expense:KZT:3f2b8c4e-1a2b-4c3d-8e9f-0a1b2c3d4e5f", 2000, Direction::AtMost)));
    EXPECT_TRUE(Habits::empty_day_is_zero(daily("practice:expense:USD", 50, Direction::AtMost)));
    EXPECT_TRUE(Habits::empty_day_is_zero(daily("tasks:done", 3, Direction::AtMost)));
    EXPECT_FALSE(Habits::empty_day_is_zero(daily("food:kcal", 2200, Direction::AtMost)));
    EXPECT_FALSE(Habits::empty_day_is_zero(daily("money:expense:KZT", 5000)));
    EXPECT_FALSE(Habits::empty_day_is_zero(daily("money:balance:KZT", 1000, Direction::AtMost)));
    EXPECT_FALSE(Habits::empty_day_is_zero(daily(nullptr)));

    // Spend at most 5000: 2026-10-12 had 7000, 2026-10-13 nothing; filled up to 2026-10-13.
    const Habit spend = daily("money:expense:KZT", 5000, Direction::AtMost);
    Habits::Values spent{{d("2026-10-12"), 7000}};
    Habits::fill_empty_days(spend, spent, d("2026-10-11"), d("2026-10-13"));
    EXPECT_EQ(Habits::done_days(spend, {}, spent), days({"2026-10-11", "2026-10-13"}))
        << "the day without spending is done, the 7000 day is not";
    EXPECT_DOUBLE_EQ(spent.at(d("2026-10-12")), 7000) << "a day with a row keeps its value";
    EXPECT_EQ(Habits::streak(spend, Habits::done_days(spend, {}, spent), d("2026-10-14")), 1);

    const Habit kcal = daily("food:kcal", 2200, Direction::AtMost);
    Habits::Values eaten;
    Habits::fill_empty_days(kcal, eaten, d("2026-10-11"), d("2026-10-13"));
    EXPECT_TRUE(eaten.empty()) << "food is not filled";
}

// Weight at most 80 kg: 79.6 done, 80.4 not, a record without a number not. The same
// series without a goal ticks every weighed day, the record without a number too.
TEST(HabitsEval, WeightWithGoalAtMost) {
    const Habit at_most = daily("fitness:weight", 80, Direction::AtMost);
    const Habits::Values scales{{d("2026-10-12"), 79.6}, {d("2026-10-13"), 80.4}, {d("2026-10-14"), Habits::kNoNumber}};
    EXPECT_EQ(Habits::done_days(at_most, {}, scales), days({"2026-10-12"}));
    const auto no_number = Habits::day_state(at_most, d("2026-10-14"), {}, scales);
    EXPECT_FALSE(no_number.done) << "no weight to compare";
    EXPECT_FALSE(no_number.value.has_value());

    const Habit any = daily("fitness:weight");
    EXPECT_EQ(Habits::done_days(any, {}, scales), days({"2026-10-12", "2026-10-13", "2026-10-14"}));
    const auto weighed = Habits::day_state(any, d("2026-10-13"), {}, scales);
    EXPECT_TRUE(weighed.done);
    EXPECT_FALSE(weighed.manual);
    EXPECT_DOUBLE_EQ(*weighed.value, 80.4);
    EXPECT_FALSE(Habits::day_state(any, d("2026-10-11"), {}, scales).done) << "no weighing that day";
}

// Presence entries count by being there: a mood, tape, a task done, a habit ticked by hand.
TEST(HabitsEval, PresenceWithoutGoal) {
    for (const char* id : {"habits:mood",
                           "tape:waist",
                           "tasks:done",
                           "fitness:resting_hr",
                           "fitness:band_workouts",
                           "habits:3f2b8c4e-1a2b-4c3d-8e9f-0a1b2c3d4e5f"}) {
        const Habit h = daily(id);
        EXPECT_TRUE(Habits::meets(h, 1)) << id;
        EXPECT_TRUE(Habits::meets(h, Habits::kNoNumber)) << id << ": a tick without a value is still a tick";
    }
    for (const char* id : {"food:kcal", "fitness:sleep", "money:expense:KZT", "fitness:hr_avg"}) {
        EXPECT_FALSE(Habits::meets(daily(id), 1)) << id << " needs a goal";
    }
}

// A day short of the goal is still done by a tick by hand, at least and at most alike.
TEST(HabitsEval, ManualTickWinsOverData) {
    const Habit kcal = daily("food:kcal", 2200, Direction::AtMost);
    const Habits::Checks ticked{{d("2026-10-13"), std::nullopt}};
    const Habits::Values eaten{{d("2026-10-13"), 2600}};
    const auto s = Habits::day_state(kcal, d("2026-10-13"), ticked, eaten);
    EXPECT_TRUE(s.done) << "2600 is over 2200, the tick wins";
    EXPECT_TRUE(s.manual);
    EXPECT_DOUBLE_EQ(*s.value, 2600) << "no value on the tick: the source's value shows";
    EXPECT_EQ(Habits::done_days(kcal, ticked, eaten), days({"2026-10-13"}));
}

// Five habits, three on fitness:steps (one under its 1.33 name), one on food:kcal, one by
// hand: two reads.
TEST(HabitsEval, OneReadPerSharedSource) {
    std::vector<Habit> habits{daily("fitness:steps", 8000),
                              daily("food:kcal", 2200, Direction::AtMost),
                              daily("steps", 10000),
                              daily(nullptr),
                              daily("fitness:steps", 12000)};
    EXPECT_EQ(Habits::distinct_sources(habits), (std::vector<std::string>{"fitness:steps", "food:kcal"}));
    EXPECT_TRUE(Habits::distinct_sources({daily(nullptr)}).empty());
    // A habit and a category are kept apart by their parameters.
    habits.push_back(daily("habits:3f2b8c4e-1a2b-4c3d-8e9f-0a1b2c3d4e5f"));
    habits.push_back(daily("habits:3F2B8C4E-1A2B-4C3D-8E9F-0A1B2C3D4E5F"));
    habits.push_back(daily("money:expense:KZT", 5000, Direction::AtMost));
    habits.push_back(daily("money:expense:USD", 50, Direction::AtMost));
    EXPECT_EQ(Habits::distinct_sources(habits).size(), 5U) << "the habit id in any case is one source";
}

TEST(HabitsEval, ADayIsDoneByHandOrByItsSource) {
    const Habit steps = daily("fitness:steps", 8000);
    const std::map<Day, std::optional<double>> manual{{d("2026-10-12"), std::nullopt}, {d("2026-10-13"), 20.0}};
    const std::map<Day, double> autos{{d("2026-10-12"), 3000}, {d("2026-10-14"), 9312}, {d("2026-10-11"), 7000}};

    const auto by_hand = Habits::day_state(steps, d("2026-10-12"), manual, autos);
    EXPECT_TRUE(by_hand.done) << "a manual tick wins over 3000 steps";
    EXPECT_TRUE(by_hand.manual);
    ASSERT_TRUE(by_hand.value.has_value());
    EXPECT_DOUBLE_EQ(*by_hand.value, 3000) << "no value on the tick: the source's value shows";

    const auto with_value = Habits::day_state(steps, d("2026-10-13"), manual, autos);
    EXPECT_TRUE(with_value.done);
    EXPECT_DOUBLE_EQ(*with_value.value, 20.0) << "the tick's own value";

    const auto by_data = Habits::day_state(steps, d("2026-10-14"), manual, autos);
    EXPECT_TRUE(by_data.done) << "9312 >= 8000";
    EXPECT_FALSE(by_data.manual);
    EXPECT_DOUBLE_EQ(*by_data.value, 9312);

    const auto short_day = Habits::day_state(steps, d("2026-10-11"), manual, autos);
    EXPECT_FALSE(short_day.done) << "7000 < 8000";
    EXPECT_DOUBLE_EQ(*short_day.value, 7000);

    const auto empty = Habits::day_state(steps, d("2026-10-10"), manual, autos);
    EXPECT_FALSE(empty.done);
    EXPECT_FALSE(empty.value.has_value());

    const auto done = Habits::done_days(steps, manual, autos);
    EXPECT_EQ(done, days({"2026-10-12", "2026-10-13", "2026-10-14"}));
}

// ── the daily streak ────────────────────────────────────────────────────────

// Done Sep 28, 29, 30, Oct 1, Oct 2. On Oct 3 nothing yet: the open today does not
// break the run, 5 days. Ticked on Oct 3: 6.
TEST(HabitsEval, DailyStreakAcrossAMonthWithAnOpenToday) {
    auto done = days({"2026-09-28", "2026-09-29", "2026-09-30", "2026-10-01", "2026-10-02"});
    EXPECT_EQ(Habits::streak(daily(), done, d("2026-10-03")), 5);
    done.insert(d("2026-10-03"));
    EXPECT_EQ(Habits::streak(daily(), done, d("2026-10-03")), 6);
}

// Done Oct 1 and Oct 3 (Oct 2 missed): on Oct 4 the run is Oct 3 alone, 1. On Oct 5
// with Oct 4 missed: 0.
TEST(HabitsEval, DailyStreakBreaksOnAMissedDay) {
    const auto done = days({"2026-10-01", "2026-10-03"});
    EXPECT_EQ(Habits::streak(daily(), done, d("2026-10-04")), 1);
    EXPECT_EQ(Habits::streak(daily(), done, d("2026-10-05")), 0);
    EXPECT_EQ(Habits::streak(daily(), {}, d("2026-10-05")), 0);
    EXPECT_EQ(Habits::streak(daily(), days({"2026-10-05"}), d("2026-10-05")), 1) << "today alone";
}

// Dec 30, Dec 31, Jan 1: on Jan 2 (open) the run is 3 across New Year.
TEST(HabitsEval, DailyStreakAcrossTheYear) {
    const auto done = days({"2025-12-30", "2025-12-31", "2026-01-01"});
    EXPECT_EQ(Habits::streak(daily(), done, d("2026-01-02")), 3);
    EXPECT_EQ(Habits::streak(daily(), done, d("2026-01-01")), 3) << "Jan 1 done itself";
}

// Runs: Sep 1–3 (3 days), Sep 10–14 (5 days), Oct 1–2 (2 days, the current one on Oct 3).
TEST(HabitsEval, TheBestDailyRun) {
    const auto done = days({"2026-09-01",
                            "2026-09-02",
                            "2026-09-03",
                            "2026-09-10",
                            "2026-09-11",
                            "2026-09-12",
                            "2026-09-13",
                            "2026-09-14",
                            "2026-10-01",
                            "2026-10-02"});
    EXPECT_EQ(Habits::best(daily(), done), 5);
    EXPECT_EQ(Habits::streak(daily(), done, d("2026-10-03")), 2);
    EXPECT_EQ(Habits::best(daily(), {}), 0);
    // A run across the year: Dec 31 and Jan 1 are neighbours.
    EXPECT_EQ(Habits::best(daily(), days({"2025-12-31", "2026-01-01", "2026-01-03"})), 2);
}

// ── the week ────────────────────────────────────────────────────────────────

TEST(HabitsEval, TheWeekRunsMondayToSunday) {
    EXPECT_EQ(Habits::monday_of(d("2026-10-14")), d("2026-10-12")) << "Wednesday";
    EXPECT_EQ(Habits::monday_of(d("2026-10-12")), d("2026-10-12")) << "Monday itself";
    EXPECT_EQ(Habits::monday_of(d("2026-10-18")), d("2026-10-12")) << "Sunday ends the week";
    EXPECT_EQ(Habits::monday_of(d("2026-10-19")), d("2026-10-19"));
    EXPECT_EQ(Habits::monday_of(d("2026-01-01")), d("2025-12-29")) << "a Thursday; the week starts in 2025";
    EXPECT_EQ(Habits::monday_of(d("2026-03-01")), d("2026-02-23")) << "a Sunday";
}

// Week of Oct 12: Sun Oct 11 is last week, Mon Oct 19 the next.
TEST(HabitsEval, AWeekCountsItsOwnDays) {
    const auto done = days({"2026-10-11", "2026-10-12", "2026-10-14", "2026-10-18", "2026-10-19"});
    EXPECT_EQ(Habits::week_count(done, d("2026-10-15")), 3) << "Oct 12, 14, 18";
    EXPECT_EQ(Habits::week_count(done, d("2026-10-11")), 1);
}

// Three times a week. Dec 15–21: 3 done; Dec 22–28: 3; Dec 29–Jan 4: Dec 30, Dec 31,
// Jan 2 = 3; this week Jan 5–11 on Wed Jan 7: Jan 5 only, open. Streak 3 met weeks
// ending last week. With Jan 6 and Jan 7 done the week is met: 4.
TEST(HabitsEval, WeeklyStreakAcrossNewYear) {
    const Habit h = weekly(3);
    auto done = days({"2025-12-15",
                      "2025-12-17",
                      "2025-12-19",
                      "2025-12-22",
                      "2025-12-23",
                      "2025-12-28",
                      "2025-12-30",
                      "2025-12-31",
                      "2026-01-02",
                      "2026-01-05"});
    EXPECT_EQ(Habits::streak(h, done, d("2026-01-07")), 3);
    EXPECT_EQ(Habits::week_count(done, d("2026-01-07")), 1);
    done.insert(d("2026-01-06"));
    done.insert(d("2026-01-07"));
    EXPECT_EQ(Habits::streak(h, done, d("2026-01-07")), 4);
    EXPECT_EQ(Habits::best(h, done), 4);
}

// Twice a week. Sep 28–Oct 4: 2 (met); Oct 5–11: 1 (missed); this week Oct 12–18 on
// Oct 14: 2 (met). Streak: last week missed, this week met = 1. Best: 1.
TEST(HabitsEval, AMissedWeekBreaksTheWeeklyStreak) {
    const Habit h = weekly(2);
    const auto done = days({"2026-09-28", "2026-10-02", "2026-10-07", "2026-10-12", "2026-10-13"});
    EXPECT_EQ(Habits::streak(h, done, d("2026-10-14")), 1);
    EXPECT_EQ(Habits::best(h, done), 1);
    EXPECT_EQ(Habits::streak(h, done, d("2026-10-19")), 1) << "next Monday: last week met, the new one open";
    EXPECT_EQ(Habits::streak(h, {}, d("2026-10-19")), 0);
}

// Once a week, every week from Aug 31 to Oct 11 (6 weeks), then Oct 19–25 met after
// a gap of Oct 12–18: best 6.
TEST(HabitsEval, TheBestWeeklyRun) {
    const auto done = days({"2026-08-31",
                            "2026-09-08",
                            "2026-09-16",
                            "2026-09-24",
                            "2026-09-27",
                            "2026-10-01",
                            "2026-10-11",
                            "2026-10-20"});
    // Weeks: Aug 31, Sep 7, Sep 14, Sep 21 (24 and 27), Sep 28, Oct 5 — six in a row.
    EXPECT_EQ(Habits::best(weekly(1), done), 6);
    EXPECT_EQ(Habits::best(weekly(2), done), 1) << "only the week of Sep 21 has two days";
}

// ── the Now plate ───────────────────────────────────────────────────────────

// Five daily habits, three done; two weekly: Swimming 2 of 3 (not today), Workout
// 4 of 4 (met earlier this week). Done: 3 daily + Workout (met) = 4 of 7. Left: the
// two open daily habits in order.
TEST(HabitsEval, ThePlateCountsTheDay) {
    const std::vector<Habits::PlateItem> items{
        {"Sleep 8 h", Schedule::Daily, 0, false, 0},
        {"Steps 8000", Schedule::Daily, 0, true, 0},
        {"Meditation", Schedule::Daily, 0, false, 0},
        {"Reading", Schedule::Daily, 0, true, 0},
        {"Wake up by 7:00", Schedule::Daily, 0, true, 0},
        {"Swimming", Schedule::Weekly, 3, false, 2},
        {"Workout", Schedule::Weekly, 4, false, 4},
    };
    const json p = Habits::plate(items);
    EXPECT_EQ(p["done"], 4);
    EXPECT_EQ(p["total"], 7);
    EXPECT_EQ(p["left"], json::array({"Sleep 8 h", "Meditation"}));
    ASSERT_EQ(p["week"].size(), 2U);
    EXPECT_EQ(p["week"][0], (json{{"name", "Swimming"}, {"done", 2}, {"per_week", 3}}));
    EXPECT_EQ(p["week"][1], (json{{"name", "Workout"}, {"done", 4}, {"per_week", 4}}));
}

TEST(HabitsEval, AWeeklyHabitDoneTodayCountsForTheDay) {
    const json p = Habits::plate({{"Swimming", Schedule::Weekly, 3, true, 1}});
    EXPECT_EQ(p["done"], 1);
    EXPECT_EQ(p["total"], 1);
    EXPECT_TRUE(p["left"].empty()) << "left lists daily habits only";
}

// ── a daily target ──────────────────────────────────────────────────────────

// Water, target 2000 ml: 1500 on the 12th, 2000 on the 13th, 2250 on the 14th, a tick without a value on the 11th.
TEST(HabitsEval, ADailyTargetCountsADayOnlyWhenTheSumReachesIt) {
    Habit water = daily();
    water.target = 2000;
    const Habits::Checks manual{{d("2026-10-11"), std::nullopt},
                                {d("2026-10-12"), 1500.0},
                                {d("2026-10-13"), 2000.0},
                                {d("2026-10-14"), 2250.0}};
    const Habits::Values none;

    const auto short_day = Habits::day_state(water, d("2026-10-12"), manual, none);
    EXPECT_FALSE(short_day.done) << "1500 < 2000";
    EXPECT_TRUE(short_day.manual) << "the tick is there, only short";
    EXPECT_DOUBLE_EQ(*short_day.value, 1500);
    EXPECT_TRUE(Habits::day_state(water, d("2026-10-13"), manual, none).done) << "2000 reaches 2000";
    EXPECT_TRUE(Habits::day_state(water, d("2026-10-14"), manual, none).done);
    EXPECT_FALSE(Habits::day_state(water, d("2026-10-11"), manual, none).done) << "no value is no water";

    EXPECT_EQ(Habits::done_days(water, manual, none), days({"2026-10-13", "2026-10-14"}));
    EXPECT_EQ(Habits::streak(water, Habits::done_days(water, manual, none), d("2026-10-14")), 2)
        << "the 12th was short: the run starts on the 13th";

    // The same ticks without a target: any tick counts, as before 1.33.
    EXPECT_EQ(Habits::done_days(daily(), manual, none).size(), 4U);
    EXPECT_TRUE(Habits::check_counts(daily(), std::nullopt));
    EXPECT_FALSE(Habits::check_counts(water, 1999.9));
}

// ── mood ────────────────────────────────────────────────────────────────────

TEST(HabitsMood, TagsComeFromTheFixedSetInItsOrder) {
    const auto tags = Habits::Mood::tags_of(json::array({"tired", "calm", "tired", "focused"}));
    ASSERT_TRUE(tags.has_value());
    EXPECT_EQ(*tags, (std::vector<std::string>{"calm", "focused", "tired"})) << "repeats dropped, the set's order";
    EXPECT_TRUE(Habits::Mood::tags_of(json::array())->empty());
    EXPECT_FALSE(Habits::Mood::tags_of(json::array({"calm", "bored"})).has_value()) << "not in the set";
    EXPECT_FALSE(Habits::Mood::tags_of(json::array({"Calm"})).has_value()) << "lower case only";
    EXPECT_FALSE(Habits::Mood::tags_of(json::array({1})).has_value());
    EXPECT_FALSE(Habits::Mood::tags_of(json("calm")).has_value()) << "a list, not a word";
    EXPECT_TRUE(Habits::Mood::is_tag("sick"));
    EXPECT_FALSE(Habits::Mood::is_tag(""));
}
