/**
 * @file test_habits_eval.cpp
 * @brief The pure habit evaluation (src/habits/Habits.hpp): a day done by
 *        hand or by a source meeting its goal, the daily streak with an open
 *        today across a month and a year boundary, the Monday-to-Sunday week
 *        and its streak across New Year, the best streak, sleep and workouts
 *        placed on the local day, the Now plate, a daily target on a habit
 *        ticked by hand, and the tags of a mood check-in. The worked numbers are
 *        in the comments; 2026-10-14 is a Wednesday, 2026-01-01 a Thursday.
 */

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "habits/Habits.hpp"
#include "habits/Mood.hpp"

using Habits::Day;
using Habits::Habit;
using Habits::Schedule;
using Habits::Source;
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

Habit daily(Source source = Source::Manual, std::optional<double> goal = std::nullopt) {
    return Habit{Schedule::Daily, 0, goal, source, std::nullopt};
}

Habit weekly(int per_week, Source source = Source::Manual) {
    return Habit{Schedule::Weekly, per_week, std::nullopt, source, std::nullopt};
}

}  // namespace

// ── the day ─────────────────────────────────────────────────────────────────

TEST(HabitsEval, ParsesTheWordsOfTheModel) {
    EXPECT_TRUE(Habits::schedule_of("daily") == Schedule::Daily);
    EXPECT_TRUE(Habits::schedule_of("weekly") == Schedule::Weekly);
    EXPECT_FALSE(Habits::schedule_of("monthly").has_value());
    EXPECT_TRUE(Habits::source_of("manual") == Source::Manual);
    EXPECT_TRUE(Habits::source_of("sleep") == Source::Sleep);
    EXPECT_TRUE(Habits::source_of("steps") == Source::Steps);
    EXPECT_TRUE(Habits::source_of("active_kcal") == Source::ActiveKcal);
    EXPECT_TRUE(Habits::source_of("workout") == Source::Workout);
    EXPECT_FALSE(Habits::source_of("water").has_value());
}

// Sleep goal 8 h = 480 min; steps 8000; active kcal 500; a workout is any session.
TEST(HabitsEval, EachSourceAgainstItsGoal) {
    EXPECT_TRUE(Habits::meets(daily(Source::Sleep, 8), 480));
    EXPECT_FALSE(Habits::meets(daily(Source::Sleep, 8), 479)) << "7 h 59 m is short of 8 h";
    EXPECT_TRUE(Habits::meets(daily(Source::Sleep, 7.5), 450)) << "7.5 h = 450 min";
    EXPECT_TRUE(Habits::meets(daily(Source::Steps, 8000), 8000));
    EXPECT_FALSE(Habits::meets(daily(Source::Steps, 8000), 7999));
    EXPECT_TRUE(Habits::meets(daily(Source::ActiveKcal, 500), 512.5));
    EXPECT_FALSE(Habits::meets(daily(Source::ActiveKcal, 500), 499.9));
    EXPECT_TRUE(Habits::meets(daily(Source::Workout), 1)) << "one finished session";
    EXPECT_TRUE(Habits::meets(daily(Source::Workout, 3), 1)) << "a workout ignores the goal";
    EXPECT_FALSE(Habits::meets(daily(Source::Workout), 0));
    EXPECT_FALSE(Habits::meets(daily(Source::Manual, 45), 1000)) << "a manual habit is never done by data";
    EXPECT_FALSE(Habits::meets(daily(Source::Steps), 100000)) << "a source without a goal is never met";
}

TEST(HabitsEval, ADayIsDoneByHandOrByItsSource) {
    const Habit steps = daily(Source::Steps, 8000);
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

// ── sources on the local day ────────────────────────────────────────────────

// The region zone is UTC+8 (cn). A night ending 2026-10-13T23:30Z ends at 07:30 on
// Oct 14 local; a nap that day is left out; a second night ending later the same
// local day (Oct 14 01:00Z = 09:00 local) is the one picked, as /today picks the
// latest. A night ending 2026-10-14T16:30Z is 00:30 on Oct 15 local.
TEST(HabitsEval, SleepLandsOnTheLocalDayItEnds) {
    const json rows = json::array({
        {{"end_at", "2026-10-13T23:30:00+00:00"}, {"time_asleep_minutes", 430}, {"is_nap", false}},
        {{"end_at", "2026-10-14T05:00:00+00:00"}, {"time_asleep_minutes", 40}, {"is_nap", true}},
        {{"end_at", "2026-10-14T01:00:00+00:00"}, {"time_asleep_minutes", 462}, {"is_nap", false}},
        {{"end_at", "2026-10-14T16:30:00+00:00"}, {"time_asleep_minutes", 500}, {"is_nap", false}},
        {{"end_at", "2026-10-12T22:00:00+00:00"}, {"time_asleep_minutes", nullptr}},
    });
    const auto by_day = Habits::sleep_by_local_day(rows, 8 * 3600);
    ASSERT_EQ(by_day.size(), 2U);
    EXPECT_DOUBLE_EQ(by_day.at(d("2026-10-14")), 462) << "7 h 42 m, the later night";
    EXPECT_DOUBLE_EQ(by_day.at(d("2026-10-15")), 500);
    // In UTC the 23:30 night stays on Oct 13.
    EXPECT_DOUBLE_EQ(Habits::sleep_by_local_day(rows, 0).at(d("2026-10-13")), 430);
}

TEST(HabitsEval, StepsAndKcalByTheirDate) {
    const json rows = json::array({
        {{"date", "2026-10-13"}, {"steps", 9312}, {"active_kcal", 410.5}},
        {{"date", "2026-10-14"}, {"steps", nullptr}, {"active_kcal", 120}},
    });
    const auto steps = Habits::activity_by_day(rows, "steps");
    ASSERT_EQ(steps.size(), 1U);
    EXPECT_DOUBLE_EQ(steps.at(d("2026-10-13")), 9312);
    const auto kcal = Habits::activity_by_day(rows, "active_kcal");
    EXPECT_DOUBLE_EQ(kcal.at(d("2026-10-13")), 410.5);
    EXPECT_DOUBLE_EQ(kcal.at(d("2026-10-14")), 120);
}

// Asia/Almaty is UTC+5. A session finished 2026-10-13T20:30Z is 01:30 on Oct 14; one
// finished Oct 14 18:00Z is 23:00 on Oct 14; one finished Oct 14 19:30Z is 00:30 on
// Oct 15. Oct 14 has two sessions, Oct 15 one, Oct 13 none.
TEST(HabitsEval, AWorkoutCountsOnTheLocalDayItFinished) {
    const std::vector<Habits::Session> sessions{
        {"2026-10-13T20:30:00Z", 5 * 3600},
        {"2026-10-14T18:00:00Z", 5 * 3600},
        {"2026-10-14T19:30:00Z", 5 * 3600},
    };
    const auto by_day = Habits::workouts_by_local_day(sessions);
    EXPECT_EQ(by_day.count(d("2026-10-13")), 0U);
    EXPECT_DOUBLE_EQ(by_day.at(d("2026-10-14")), 2);
    EXPECT_DOUBLE_EQ(by_day.at(d("2026-10-15")), 1);
    EXPECT_EQ(Habits::local_day("2026-10-13T20:30:00Z", 0), d("2026-10-13")) << "in UTC";
    EXPECT_EQ(Habits::local_day("2026-10-14T02:00:00Z", -7 * 3600), d("2026-10-13")) << "west of UTC";
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
