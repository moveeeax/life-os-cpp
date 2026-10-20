/**
 * @file test_metrics_series.cpp
 * @brief The series reader (src/repositories/metrics/Series.hpp) against a real
 *        database: each family of the catalog seeded and read by local day
 *        (a night ending 00:30 in Asia/Almaty lands on that day, a weigh-in on
 *        the Berlin DST day stays on its day), the owner scope of money and
 *        habit parameters, and the reasons a series has no values.
 */

#include <cmath>
#include <map>
#include <string>

#include <gtest/gtest.h>

#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "habits/Habits.hpp"
#include "metrics/Catalog.hpp"
#include "repositories/metrics/Series.hpp"
#include "test_helpers.hpp"

namespace {

using Repositories::Metrics::Access;
using Repositories::Metrics::Why;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaa51";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbb51";
constexpr const char* kAnnaMi = "metrics-test-anna-mi";

void sql(const std::string& statement) {
    Database::get().execute_write([&](auto& txn) {
        txn.exec(statement);
        return true;
    });
}

class MetricsSeriesTest : public TestHelpers::CoreBackedTest {
protected:
    std::string config_file_name() const override { return "metrics_series_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["fitness"]["enabled"] = true;
        cfg["food"]["enabled"] = false;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            for (const char* id : {kAnna, kBoris}) {
                const std::string o = id;
                txn.exec_params(
                    "INSERT INTO users (id, email, confirmed, role_id) "
                    "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                    "ON CONFLICT DO NOTHING",
                    o,
                    o + "@example.test");
                for (const char* table : {"food_entries",
                                          "mood_checkins",
                                          "tape_measurements",
                                          "workout_sessions",
                                          "task_items",
                                          "money_transactions",
                                          "money_transfers",
                                          "money_accounts",
                                          "money_categories",
                                          "money_currencies",
                                          "practice_entries",
                                          "habits",
                                          "mi_accounts"}) {
                    txn.exec_params(std::string("DELETE FROM ") + table + " WHERE owner_id = $1::uuid", o);
                }
            }
            const std::string mi = kAnnaMi;
            for (const char* table : {"daily_activity",
                                      "sleep_sessions",
                                      "body_measurements",
                                      "heart_rate_samples",
                                      "spo2_samples",
                                      "stress_samples",
                                      "abnormal_heart_beat_events",
                                      "workouts"}) {
                txn.exec_params(std::string("DELETE FROM ") + table + " WHERE user_id = $1", mi);
            }
            return true;
        });
    }

    static Access all() {
        Access a;
        a.fitness = a.mi_read = a.workout = a.food = a.habits = a.tasks = a.money = a.practice = true;
        a.xiaomi_id = kAnnaMi;
        return a;
    }

    /// The series as date -> value (NaN kept), with its reason.
    static std::pair<std::map<std::string, double>, Why> read(const std::string& source,
                                                              const char* from,
                                                              const char* to,
                                                              const char* tz = "UTC",
                                                              const Access& a = all(),
                                                              const char* owner = kAnna) {
        const auto ref = Metrics::parse(source);
        EXPECT_TRUE(ref) << source;
        if (!ref) {
            return {{}, Why::NotYours};
        }
        const auto s = Repositories::Metrics::read(owner, *ref, Habits::day_of(from), Habits::day_of(to), tz, a);
        std::map<std::string, double> out;
        for (const auto& [day, value] : s.values) {
            out[Habits::text_of(day)] = value;
        }
        return {out, s.why};
    }

    static std::map<std::string, double> values(const std::string& source,
                                                const char* from,
                                                const char* to,
                                                const char* tz = "UTC") {
        auto [v, why] = read(source, from, to, tz);
        EXPECT_EQ(why, Why::Ok) << source;
        return v;
    }
};

}  // namespace

TEST_F(MetricsSeriesTest, StepsSumOverDevices) {
    sql(std::string("INSERT INTO daily_activity (user_id, date, device_id, steps, distance_m) VALUES ('") + kAnnaMi +
        "', '2026-10-01', 'band', 5000, 3500), ('" + kAnnaMi + "', '2026-10-01', 'phone', 3000, NULL), ('" + kAnnaMi +
        "', '2026-10-03', 'band', 0, NULL)");
    const std::map<std::string, double> steps{{"2026-10-01", 8000}, {"2026-10-03", 0}};
    EXPECT_EQ(values("fitness:steps", "2026-10-01", "2026-10-03"), steps);
    EXPECT_EQ(values("steps", "2026-10-01", "2026-10-03"), steps) << "the legacy name reads the same";
    const std::map<std::string, double> distance{{"2026-10-01", 3500}};
    EXPECT_EQ(values("fitness:distance", "2026-10-01", "2026-10-03"), distance) << "a day without the field is absent";
    EXPECT_TRUE(values("fitness:floors", "2026-10-01", "2026-10-03").empty());
    EXPECT_TRUE(values("fitness:steps", "2026-10-02", "2026-10-02").empty());
}

TEST_F(MetricsSeriesTest, SleepLatestNightByLocalEnd) {
    // Ends 2026-10-01 19:30 UTC = 2026-10-02 00:30 in Almaty (UTC+5): the night of the 2nd there.
    sql(std::string("INSERT INTO sleep_sessions (user_id, sleep_id, start_at, end_at, duration_minutes, "
                    "time_asleep_minutes, time_awake_minutes, sleep_score, is_nap) VALUES ('") +
        kAnnaMi + "', 'n1', '2026-10-01 12:30Z', '2026-10-01 19:30Z', 420, 390, 30, 80, false), ('" + kAnnaMi +
        "', 'nap', '2026-10-02 08:00Z', '2026-10-02 09:00Z', 60, 60, 0, NULL, true), ('" + kAnnaMi +
        "', 'n0', '2026-09-30 15:00Z', '2026-09-30 19:10Z', 250, 150, 100, 70, false), ('" + kAnnaMi +
        "', 'n0b', '2026-09-30 17:00Z', '2026-09-30 23:00Z', 360, 300, 60, NULL, false)");
    const auto almaty = values("fitness:sleep", "2026-10-01", "2026-10-02", "Asia/Almaty");
    ASSERT_EQ(almaty.size(), 2U);
    EXPECT_DOUBLE_EQ(almaty.at("2026-10-02"), 6.5) << "390 min asleep, the nap left out";
    // Two nights end on the 1st in Almaty (00:10 and 04:00 local): the later one counts.
    EXPECT_DOUBLE_EQ(almaty.at("2026-10-01"), 5.0);
    const auto utc = values("fitness:sleep", "2026-10-01", "2026-10-02", "UTC");
    EXPECT_EQ(utc.count("2026-10-02"), 0U) << "in UTC that night ends on the 1st";
    EXPECT_DOUBLE_EQ(utc.at("2026-10-01"), 6.5);
    EXPECT_DOUBLE_EQ(values("fitness:sleep_duration", "2026-10-02", "2026-10-02", "Asia/Almaty").at("2026-10-02"), 7.0);
    EXPECT_DOUBLE_EQ(values("fitness:sleep_awake", "2026-10-02", "2026-10-02", "Asia/Almaty").at("2026-10-02"), 30);
    EXPECT_DOUBLE_EQ(values("fitness:sleep_score", "2026-10-02", "2026-10-02", "Asia/Almaty").at("2026-10-02"), 80);
    EXPECT_TRUE(values("fitness:sleep_score", "2026-10-01", "2026-10-01", "Asia/Almaty").empty())
        << "the latest night of the 1st has no score, the earlier one's 70 does not count";
}

TEST_F(MetricsSeriesTest, WeightLastOfDay) {
    sql(std::string("INSERT INTO body_measurements (user_id, timestamp, weight_kg, bmi, body_fat_pct) VALUES ('") +
        kAnnaMi + "', '2026-10-01 06:00Z', 81.0, 25.1, 20.0), ('" + kAnnaMi +
        "', '2026-10-01 18:00Z', 80.4, NULL, 19.5), ('" + kAnnaMi + "', '2026-10-03 06:00Z', 80.0, 24.9, NULL)");
    const std::map<std::string, double> weight{{"2026-10-01", 80.4}, {"2026-10-03", 80.0}};
    EXPECT_EQ(values("fitness:weight", "2026-10-01", "2026-10-03"), weight);
    const std::map<std::string, double> bmi{{"2026-10-01", 25.1}, {"2026-10-03", 24.9}};
    EXPECT_EQ(values("fitness:bmi", "2026-10-01", "2026-10-03"), bmi) << "the day's last record that has a BMI";
    const std::map<std::string, double> fat{{"2026-10-01", 19.5}};
    EXPECT_EQ(values("fitness:body_fat", "2026-10-01", "2026-10-03"), fat);
}

TEST_F(MetricsSeriesTest, RestingHrLast) {
    sql(std::string("INSERT INTO heart_rate_samples (user_id, timestamp, bpm, sample_type) VALUES ('") + kAnnaMi +
        "', '2026-10-01 06:00Z', 58, 'resting'), ('" + kAnnaMi + "', '2026-10-01 20:00Z', 55, 'resting'), ('" +
        kAnnaMi + "', '2026-10-01 10:00Z', 70, 'passive'), ('" + kAnnaMi + "', '2026-10-01 11:00Z', 130, 'active')");
    EXPECT_DOUBLE_EQ(values("fitness:resting_hr", "2026-10-01", "2026-10-01").at("2026-10-01"), 55);
    EXPECT_DOUBLE_EQ(values("fitness:hr_avg", "2026-10-01", "2026-10-01").at("2026-10-01"), 100)
        << "(70 + 130) / 2, resting samples left out";
    EXPECT_DOUBLE_EQ(values("fitness:hr_max", "2026-10-01", "2026-10-01").at("2026-10-01"), 130);
}

TEST_F(MetricsSeriesTest, Spo2AvgAndMin) {
    sql(std::string("INSERT INTO spo2_samples (user_id, timestamp, spo2_pct) VALUES ('") + kAnnaMi +
        "', '2026-10-01 01:00Z', 97), ('" + kAnnaMi + "', '2026-10-01 02:00Z', 93), ('" + kAnnaMi +
        "', '2026-10-02 02:00Z', 99)");
    const std::map<std::string, double> avg{{"2026-10-01", 95}, {"2026-10-02", 99}};
    EXPECT_EQ(values("fitness:spo2", "2026-10-01", "2026-10-02"), avg);
    EXPECT_DOUBLE_EQ(values("fitness:spo2_min", "2026-10-01", "2026-10-01").at("2026-10-01"), 93);
    sql(std::string("INSERT INTO stress_samples (user_id, timestamp, stress_score, level) VALUES ('") + kAnnaMi +
        "', '2026-10-01 10:00Z', 20, 'low'), ('" + kAnnaMi + "', '2026-10-01 11:00Z', 60, 'medium')");
    EXPECT_DOUBLE_EQ(values("fitness:stress", "2026-10-01", "2026-10-01").at("2026-10-01"), 40);
    EXPECT_DOUBLE_EQ(values("fitness:stress_max", "2026-10-01", "2026-10-01").at("2026-10-01"), 60);
    sql(std::string("INSERT INTO abnormal_heart_beat_events (user_id, event_id, start_at, end_at) VALUES ('") +
        kAnnaMi + "', 'e1', '2026-10-01 10:00Z', '2026-10-01 10:01Z'), ('" + kAnnaMi +
        "', 'e2', '2026-10-01 12:00Z', '2026-10-01 12:01Z')");
    EXPECT_DOUBLE_EQ(values("fitness:abnormal_hr", "2026-10-01", "2026-10-01").at("2026-10-01"), 2);
}

TEST_F(MetricsSeriesTest, BandWorkoutSums) {
    sql(std::string("INSERT INTO workouts (user_id, workout_id, activity_type, start_at, end_at, duration_minutes, "
                    "distance_m, calories_kcal) VALUES ('") +
        kAnnaMi + "', 'w1', 'run', '2026-10-01 06:00Z', '2026-10-01 06:30Z', 30, 5000, 300), ('" + kAnnaMi +
        "', 'w2', 'walk', '2026-10-01 18:00Z', '2026-10-01 18:45Z', 45, NULL, 150)");
    EXPECT_DOUBLE_EQ(values("fitness:band_workouts", "2026-10-01", "2026-10-01").at("2026-10-01"), 2);
    EXPECT_DOUBLE_EQ(values("fitness:band_workout_minutes", "2026-10-01", "2026-10-01").at("2026-10-01"), 75);
    EXPECT_DOUBLE_EQ(values("fitness:band_workout_kcal", "2026-10-01", "2026-10-01").at("2026-10-01"), 450);
    EXPECT_DOUBLE_EQ(values("fitness:band_workout_distance", "2026-10-01", "2026-10-01").at("2026-10-01"), 5000);
}

TEST_F(MetricsSeriesTest, FoodSums) {
    sql(std::string(
            "INSERT INTO food_entries (owner_id, date, meal, name, kcal, protein_g, fat_g, carbs_g, fiber_g, salt_g) "
            "VALUES ('") +
        kAnna + "', '2026-10-01', 'lunch', 'Soup', 400, 20, 10, 50, 5, NULL), ('" + kAnna +
        "', '2026-10-01', 'dinner', 'Steak', 700, 60, 40, 0, NULL, 1.5), ('" + kAnna +
        "', '2026-10-02', 'snack', 'Tea', 0, 0, 0, 0, NULL, NULL), ('" + kBoris +
        "', '2026-10-01', 'lunch', 'Cake', 900, 5, 40, 120, 1, 1)");
    const std::map<std::string, double> kcal{{"2026-10-01", 1100}, {"2026-10-02", 0}};
    EXPECT_EQ(values("food:kcal", "2026-10-01", "2026-10-02"), kcal) << "Boris's cake is not Anna's";
    EXPECT_DOUBLE_EQ(values("food:protein", "2026-10-01", "2026-10-01").at("2026-10-01"), 80);
    const std::map<std::string, double> fiber{{"2026-10-01", 5}};
    EXPECT_EQ(values("food:fiber", "2026-10-01", "2026-10-02"), fiber) << "entries without fiber are skipped";
    EXPECT_DOUBLE_EQ(values("food:salt", "2026-10-01", "2026-10-01").at("2026-10-01"), 1.5);
}

TEST_F(MetricsSeriesTest, MoodValue) {
    sql(std::string("INSERT INTO mood_checkins (owner_id, date, score) VALUES ('") + kAnna + "', '2026-10-01', 4), ('" +
        kBoris + "', '2026-10-02', 1)");
    const std::map<std::string, double> mood{{"2026-10-01", 4}};
    EXPECT_EQ(values("habits:mood", "2026-10-01", "2026-10-02"), mood);
}

TEST_F(MetricsSeriesTest, TapeLastOfDay) {
    sql(std::string("INSERT INTO tape_measurements (owner_id, date, waist_cm, chest_cm, created_at) VALUES ('") +
        kAnna + "', '2026-10-01', 90.5, NULL, '2026-10-01 08:00Z'), ('" + kAnna +
        "', '2026-10-01', 89.0, 101, '2026-10-01 20:00Z'), ('" + kAnna +
        "', '2026-10-02', NULL, 100, '2026-10-02 08:00Z')");
    const std::map<std::string, double> waist{{"2026-10-01", 89.0}};
    EXPECT_EQ(values("tape:waist", "2026-10-01", "2026-10-02"), waist);
    const std::map<std::string, double> chest{{"2026-10-01", 101}, {"2026-10-02", 100}};
    EXPECT_EQ(values("tape:chest", "2026-10-01", "2026-10-02"), chest);
}

TEST_F(MetricsSeriesTest, WorkoutSessionsMinutesVolumeSets) {
    // Two sessions finishing 2026-10-01 in UTC; the second at 21:30 UTC is the 2nd in Almaty.
    sql(std::string("INSERT INTO workout_sessions (id, owner_id, started_at, finished_at, bodyweight_kg) VALUES "
                    "('51515151-0000-4000-8000-000000000001', '") +
        kAnna +
        "', '2026-10-01 10:00Z', '2026-10-01 11:00Z', 80), "
        "('51515151-0000-4000-8000-000000000002', '" +
        kAnna +
        "', '2026-10-01 21:00Z', '2026-10-01 21:30Z', 80), "
        "('51515151-0000-4000-8000-000000000003', '" +
        kAnna + "', '2026-10-01 12:00Z', NULL, 80)");
    sql("INSERT INTO session_exercises (id, session_id, exercise_id, position) VALUES "
        "('52525252-0000-4000-8000-000000000001', '51515151-0000-4000-8000-000000000001', '3_4_Sit-Up', 1), "
        "('52525252-0000-4000-8000-000000000002', '51515151-0000-4000-8000-000000000001', 'Ab_Roller', 2)");
    sql("INSERT INTO workout_sets (id, session_exercise_id, position, kind, weight_kg, reps) VALUES "
        "('53535353-0000-4000-8000-000000000001', '52525252-0000-4000-8000-000000000001', 1, 'work', 10, 10), "
        "('53535353-0000-4000-8000-000000000002', '52525252-0000-4000-8000-000000000001', 2, 'warmup', 0, 10), "
        "('53535353-0000-4000-8000-000000000003', '52525252-0000-4000-8000-000000000002', 1, 'work', 20, 5)");
    const std::map<std::string, double> sessions{{"2026-10-01", 2}};
    EXPECT_EQ(values("workout:sessions", "2026-10-01", "2026-10-02"), sessions) << "the open session does not count";
    EXPECT_EQ(values("workout", "2026-10-01", "2026-10-02"), sessions) << "the legacy name";
    const std::map<std::string, double> almaty{{"2026-10-01", 1}, {"2026-10-02", 1}};
    EXPECT_EQ(values("workout:sessions", "2026-10-01", "2026-10-02", "Asia/Almaty"), almaty);
    EXPECT_DOUBLE_EQ(values("workout:minutes", "2026-10-01", "2026-10-01").at("2026-10-01"), 90);
    // 10 x (10 + 80 body weight) on the bodyweight exercise, 5 x 20 on the other; the warm-up left out.
    EXPECT_DOUBLE_EQ(values("workout:volume", "2026-10-01", "2026-10-01").at("2026-10-01"), 1000);
    EXPECT_DOUBLE_EQ(values("workout:sets", "2026-10-01", "2026-10-01").at("2026-10-01"), 2);
}

TEST_F(MetricsSeriesTest, TasksDoneByLocalDay) {
    sql(std::string("INSERT INTO task_items (owner_id, title, area, status, completed_at) VALUES ('") + kAnna +
        "', 'a', 'growth', 'done', '2026-10-01 19:30Z'), ('" + kAnna +
        "', 'b', 'growth', 'done', '2026-10-01 10:00Z'), ('" + kAnna + "', 'c', 'growth', 'open', NULL), ('" + kBoris +
        "', 'd', 'growth', 'done', '2026-10-01 10:00Z')");
    const std::map<std::string, double> utc{{"2026-10-01", 2}};
    EXPECT_EQ(values("tasks:done", "2026-10-01", "2026-10-02"), utc);
    const std::map<std::string, double> almaty{{"2026-10-01", 1}, {"2026-10-02", 1}};
    EXPECT_EQ(values("tasks:done", "2026-10-01", "2026-10-02", "Asia/Almaty"), almaty);
}

namespace {

void seed_money() {
    for (const char* o : {kAnna, kBoris}) {
        sql(std::string("INSERT INTO money_currencies (owner_id, code) VALUES ('") + o + "', 'KZT'), ('" + o +
            "', 'EUR')");
    }
    sql(std::string("INSERT INTO money_accounts (id, owner_id, name, currency, opening_balance) VALUES "
                    "('54545454-0000-4000-8000-000000000001', '") +
        kAnna +
        "', 'Card', 'KZT', 10000), "
        "('54545454-0000-4000-8000-000000000002', '" +
        kAnna +
        "', 'Cash', 'KZT', 500), "
        "('54545454-0000-4000-8000-000000000003', '" +
        kAnna +
        "', 'Euro', 'EUR', 100), "
        "('54545454-0000-4000-8000-000000000009', '" +
        kBoris + "', 'Card', 'KZT', 0)");
    sql(std::string("INSERT INTO money_categories (id, owner_id, name, kind) VALUES "
                    "('55555555-0000-4000-8000-000000000001', '") +
        kAnna +
        "', 'Food', 'expense'), "
        "('55555555-0000-4000-8000-000000000002', '" +
        kAnna +
        "', 'Salary', 'income'), "
        "('55555555-0000-4000-8000-000000000003', '" +
        kAnna +
        "', 'Transport', 'expense'), "
        "('55555555-0000-4000-8000-000000000009', '" +
        kBoris + "', 'Food', 'expense')");
    sql(std::string(
            "INSERT INTO money_transactions (owner_id, type, date, account_id, amount, category_id, name, status) "
            "VALUES ('") +
        kAnna +
        "', 'expense', '2026-10-01', '54545454-0000-4000-8000-000000000001', 1500, "
        "'55555555-0000-4000-8000-000000000001', 'Lunch', 'posted'), ('" +
        kAnna +
        "', 'expense', '2026-10-01', '54545454-0000-4000-8000-000000000002', 200, "
        "'55555555-0000-4000-8000-000000000003', 'Bus', 'posted'), ('" +
        kAnna +
        "', 'expense', '2026-10-01', '54545454-0000-4000-8000-000000000001', 999, "
        "'55555555-0000-4000-8000-000000000001', 'Pending', 'pending'), ('" +
        kAnna +
        "', 'expense', '2026-10-02', '54545454-0000-4000-8000-000000000003', 7, "
        "'55555555-0000-4000-8000-000000000001', 'Coffee', 'posted'), ('" +
        kAnna +
        "', 'income', '2026-10-02', '54545454-0000-4000-8000-000000000001', 5000, "
        "'55555555-0000-4000-8000-000000000002', 'Pay', 'posted'), ('" +
        kBoris +
        "', 'expense', '2026-10-01', '54545454-0000-4000-8000-000000000009', 3000, "
        "'55555555-0000-4000-8000-000000000009', 'Boris', 'posted')");
}

}  // namespace

TEST_F(MetricsSeriesTest, MoneyExpenseByCurrencyAndCategory) {
    seed_money();
    const std::map<std::string, double> kzt{{"2026-10-01", 1700}};
    EXPECT_EQ(values("money:expense:KZT", "2026-10-01", "2026-10-02"), kzt) << "posted only, KZT accounts only";
    const std::map<std::string, double> eur{{"2026-10-02", 7}};
    EXPECT_EQ(values("money:expense:EUR", "2026-10-01", "2026-10-02"), eur);
    const std::map<std::string, double> food{{"2026-10-01", 1500}};
    EXPECT_EQ(values("money:expense:KZT:55555555-0000-4000-8000-000000000001", "2026-10-01", "2026-10-02"), food);
    const std::map<std::string, double> income{{"2026-10-02", 5000}};
    EXPECT_EQ(values("money:income:KZT", "2026-10-01", "2026-10-02"), income);
    EXPECT_EQ(read("money:expense:USD", "2026-10-01", "2026-10-02").second, Why::NotYours) << "no USD account";
}

TEST_F(MetricsSeriesTest, MoneyBalanceEndOfDay) {
    seed_money();
    // KZT: 10000 + 500 opening; -1700 on the 1st, +5000 on the 2nd; the pending row is not money yet.
    const std::map<std::string, double> kzt{
        {"2026-09-30", 10500}, {"2026-10-01", 8800}, {"2026-10-02", 13800}, {"2026-10-03", 13800}};
    EXPECT_EQ(values("money:balance:KZT", "2026-09-30", "2026-10-03"), kzt);
    const std::map<std::string, double> eur{{"2026-10-01", 100}, {"2026-10-02", 93}};
    EXPECT_EQ(values("money:balance:EUR", "2026-10-01", "2026-10-02"), eur);
}

TEST_F(MetricsSeriesTest, PracticeIncome) {
    sql(std::string("INSERT INTO practice_entries (owner_id, kind, date, amount, currency) VALUES ('") + kAnna +
        "', 'income', '2026-10-01', 1000, 'USD'), ('" + kAnna + "', 'income', '2026-10-01', 250, 'USD'), ('" + kAnna +
        "', 'income', '2026-10-01', 90, 'EUR'), ('" + kAnna + "', 'expense', '2026-10-01', 30, 'USD'), ('" + kBoris +
        "', 'income', '2026-10-01', 7777, 'USD')");
    const std::map<std::string, double> usd{{"2026-10-01", 1250}};
    EXPECT_EQ(values("practice:income:USD", "2026-10-01", "2026-10-01"), usd);
    const std::map<std::string, double> spent{{"2026-10-01", 30}};
    EXPECT_EQ(values("practice:expense:USD", "2026-10-01", "2026-10-01"), spent);
}

TEST_F(MetricsSeriesTest, ManualHabitValue) {
    sql(std::string("INSERT INTO habits (id, owner_id, name, schedule, source, unit) VALUES "
                    "('56565656-0000-4000-8000-000000000001', '") +
        kAnna +
        "', 'Water', 'daily', 'manual', 'l'), "
        "('56565656-0000-4000-8000-000000000002', '" +
        kAnna + "', 'Train', 'daily', 'workout', '')");
    sql(std::string("INSERT INTO habit_checks (owner_id, habit_id, date, value) VALUES ('") + kAnna +
        "', '56565656-0000-4000-8000-000000000001', '2026-10-01', 2.5), ('" + kAnna +
        "', '56565656-0000-4000-8000-000000000001', '2026-10-02', NULL)");
    const auto [water, why] = read("habits:56565656-0000-4000-8000-000000000001", "2026-10-01", "2026-10-03");
    EXPECT_EQ(why, Why::Ok);
    ASSERT_EQ(water.size(), 2U);
    EXPECT_DOUBLE_EQ(water.at("2026-10-01"), 2.5);
    EXPECT_TRUE(std::isnan(water.at("2026-10-02"))) << "a tick without a value is there with no number";
    EXPECT_EQ(read("habits:56565656-0000-4000-8000-000000000002", "2026-10-01", "2026-10-03").second, Why::NotYours)
        << "a habit with a source of its own is no series";
    EXPECT_EQ(
        read("habits:56565656-0000-4000-8000-000000000001", "2026-10-01", "2026-10-03", "UTC", all(), kBoris).second,
        Why::NotYours);
}

TEST_F(MetricsSeriesTest, ForeignCategoryIsNotYours) {
    seed_money();
    const auto [v, why] = read("money:expense:KZT:55555555-0000-4000-8000-000000000009", "2026-10-01", "2026-10-02");
    EXPECT_EQ(why, Why::NotYours);
    EXPECT_TRUE(v.empty()) << "Boris's rows never reach Anna";
    EXPECT_EQ(read("money:expense:KZT:55555555-0000-4000-8000-0000000000ff", "2026-10-01", "2026-10-02").second,
              Why::NotYours);
}

TEST_F(MetricsSeriesTest, ModuleOffGivesWhy) {
    sql(std::string("INSERT INTO food_entries (owner_id, date, meal, name, kcal) VALUES ('") + kAnna +
        "', '2026-10-01', 'lunch', 'Soup', 400)");
    Access a = all();
    a.food = false;
    const auto [food, why] = read("food:kcal", "2026-10-01", "2026-10-01", "UTC", a);
    EXPECT_EQ(why, Why::ModuleOff);
    EXPECT_TRUE(food.empty());

    a = all();
    a.mi_read = false;
    EXPECT_EQ(read("fitness:steps", "2026-10-01", "2026-10-01", "UTC", a).second, Why::NoPermission);
    a = all();
    a.xiaomi_id.clear();
    EXPECT_EQ(read("fitness:steps", "2026-10-01", "2026-10-01", "UTC", a).second, Why::NotLinked);
    a = all();
    a.fitness = false;
    EXPECT_EQ(read("fitness:steps", "2026-10-01", "2026-10-01", "UTC", a).second, Why::ModuleOff);
    EXPECT_EQ(read("tape:waist", "2026-10-01", "2026-10-01", "UTC", a).second, Why::ModuleOff);

    // access_of reads the switches of the config (fitness on, food off), the permission and the link.
    const auto none = Repositories::Metrics::access_of(kAnna, Domain::Permission::kGeneral);
    EXPECT_TRUE(none.fitness);
    EXPECT_FALSE(none.food);
    EXPECT_FALSE(none.mi_read);
    EXPECT_TRUE(none.xiaomi_id.empty());
    EXPECT_EQ(Repositories::Metrics::available(kAnna, *Metrics::parse("fitness:steps"), none), Why::NoPermission);
    EXPECT_EQ(Repositories::Metrics::available(kAnna, *Metrics::parse("food:kcal"), none), Why::ModuleOff);
    const auto unlinked =
        Repositories::Metrics::access_of(kAnna, Domain::Permission::kGeneral | Domain::Permission::kFitnessRead);
    EXPECT_EQ(Repositories::Metrics::available(kAnna, *Metrics::parse("fitness:steps"), unlinked), Why::NotLinked);
    sql(std::string("INSERT INTO mi_accounts (owner_id, xiaomi_user_id, pass_token_sealed, nonce, region) VALUES ('") +
        kAnna + "', '" + kAnnaMi + "', 'x', 'x', 'sg')");
    const auto linked =
        Repositories::Metrics::access_of(kAnna, Domain::Permission::kGeneral | Domain::Permission::kFitnessRead);
    EXPECT_EQ(linked.xiaomi_id, kAnnaMi);
    EXPECT_EQ(Repositories::Metrics::available(kAnna, *Metrics::parse("fitness:steps"), linked), Why::Ok);
}

TEST_F(MetricsSeriesTest, DstDayInBerlin) {
    // 2026-10-25: Berlin leaves summer time at 01:00 UTC (03:00 CEST -> 02:00 CET), a 25-hour day.
    // 2026-10-24 22:30 UTC = 00:30 CEST on the 25th; 2026-10-25 22:30 UTC = 23:30 CET on the 25th;
    // 2026-10-25 23:30 UTC = 00:30 CET on the 26th.
    sql(std::string("INSERT INTO body_measurements (user_id, timestamp, weight_kg) VALUES ('") + kAnnaMi +
        "', '2026-10-24 22:30Z', 80.0), ('" + kAnnaMi + "', '2026-10-25 22:30Z', 79.5), ('" + kAnnaMi +
        "', '2026-10-25 23:30Z', 79.0)");
    const std::map<std::string, double> berlin{{"2026-10-25", 79.5}, {"2026-10-26", 79.0}};
    EXPECT_EQ(values("fitness:weight", "2026-10-24", "2026-10-26", "Europe/Berlin"), berlin);
    const std::map<std::string, double> utc{{"2026-10-24", 80.0}, {"2026-10-25", 79.0}};
    EXPECT_EQ(values("fitness:weight", "2026-10-24", "2026-10-26", "UTC"), utc);
    sql(std::string("INSERT INTO sleep_sessions (user_id, sleep_id, start_at, end_at, duration_minutes, "
                    "time_asleep_minutes, time_awake_minutes, is_nap) VALUES ('") +
        kAnnaMi + "', 'dst', '2026-10-24 21:00Z', '2026-10-25 05:00Z', 480, 480, 0, false)");
    const std::map<std::string, double> night{{"2026-10-25", 8.0}};
    EXPECT_EQ(values("fitness:sleep", "2026-10-24", "2026-10-26", "Europe/Berlin"), night);
}
