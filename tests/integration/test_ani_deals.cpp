/**
 * @file test_ani_deals.cpp
 * @brief Promises to Ani for a week (repositories/agent/DealRepository.hpp,
 *        agent/DealTools.hpp): progress from workouts, focus minutes and a
 *        habit's ticks of that week only, five per week, only the owner's,
 *        and the turn's note with how far each is.
 */

#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/DealTools.hpp"
#include "database/Database.hpp"
#include "repositories/agent/DealRepository.hpp"
#include "test_helpers.hpp"

namespace {

using nlohmann::json;
using Repositories::Agent::DealRepository;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaade01";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbde01";
constexpr const char* kHabit = "11111111-1111-4111-8111-11111111de01";

class AniDeals : public TestHelpers::CoreBackedTest {
protected:
    DealRepository deals;
    std::string config_file_name() const override { return "ani_deals_test_config.json"; }
    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec_params("DELETE FROM ani_deals WHERE owner_id IN ($1::uuid, $2::uuid)",
                            std::string(kAnna),
                            std::string(kBoris));
            txn.exec_params("DELETE FROM focus_sessions WHERE owner_id = $1::uuid", std::string(kAnna));
            txn.exec_params("DELETE FROM workout_sessions WHERE owner_id = $1::uuid", std::string(kAnna));
            txn.exec_params("DELETE FROM habits WHERE owner_id = $1::uuid", std::string(kAnna));
            txn.exec_params(
                "INSERT INTO habits (id, owner_id, name, schedule) VALUES ($1::uuid, $2::uuid, "
                "'Meditation', 'daily')",
                std::string(kHabit),
                std::string(kAnna));
            return true;
        });
    }
    void exec(const std::string& sql) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(sql, std::string(kAnna));
            return true;
        });
    }
};

}  // namespace

TEST_F(AniDeals, ProgressComesFromTheWeeksRows) {
    ASSERT_TRUE(deals.add(kAnna, "UTC", "Зал три раза", "workouts", 3, std::nullopt));
    ASSERT_TRUE(deals.add(kAnna, "UTC", "Пять часов фокуса", "focus_minutes", 300, std::nullopt));
    ASSERT_TRUE(deals.add(kAnna, "UTC", "Медитация каждый день", "habit", 7, std::string(kHabit)));
    exec(
        "INSERT INTO workout_sessions (owner_id, started_at, finished_at) VALUES ($1::uuid, now() - interval "
        "'1 minute', now())");
    exec("INSERT INTO workout_sessions (owner_id, started_at) VALUES ($1::uuid, now())");  // not finished
    exec(
        "INSERT INTO focus_sessions (owner_id, started_at, ended_at, minutes) VALUES ($1::uuid, now() - interval "
        "'1 minute', now(), 25)");
    exec(
        "INSERT INTO focus_sessions (owner_id, started_at, ended_at, minutes) VALUES ($1::uuid, now() - interval "
        "'15 days', now() - interval '15 days' + interval '40 minutes', 40)");  // another week
    exec(
        "INSERT INTO habit_checks (owner_id, habit_id, date) VALUES ($1::uuid, '11111111-1111-4111-8111-11111111de01', "
        "(now() AT TIME ZONE 'UTC')::date)");
    const json week = deals.week(kAnna, "UTC");
    ASSERT_EQ(week.size(), 3u);
    EXPECT_EQ(week[0]["done"], 1) << "one finished workout";
    EXPECT_EQ(week[1]["done"], 25) << "this week's focus only";
    EXPECT_EQ(week[2]["done"], 1);
    EXPECT_EQ(week[2]["habit"], "Meditation");
    EXPECT_TRUE(deals.week(kBoris, "UTC").empty()) << "only the owner's";
}

TEST_F(AniDeals, FivePerWeekAndOnlyOwnHabits) {
    for (int i = 0; i < DealRepository::kMaxPerWeek; ++i) {
        ASSERT_TRUE(deals.add(kAnna, "UTC", "p" + std::to_string(i), "workouts", 1, std::nullopt));
    }
    EXPECT_FALSE(deals.add(kAnna, "UTC", "one more", "workouts", 1, std::nullopt));
    EXPECT_FALSE(deals.add(kBoris, "UTC", "her habit", "habit", 1, std::string(kHabit))) << "not his habit";
}

TEST_F(AniDeals, PromiseRunsAtOnceAndTheNoteShowsProgress) {
    auto run =
        Agent::DealTools::runner(kAnna, "UTC", [](const std::string&, const json&) { return std::string("rest"); });
    EXPECT_EQ(run("habits", json::object()), "rest");
    const json made = json::parse(
        run("promise", json{{"text", "Медитация"}, {"kind", "habit"}, {"target", 5}, {"habit", "meditation"}}));
    ASSERT_TRUE(made.contains("promised")) << made.dump();
    EXPECT_EQ(
        json::parse(run("promise", json{{"text", "x"}, {"kind", "habit"}, {"target", 5}, {"habit", "Yoga"}}))["error"],
        "unknown_habit");
    EXPECT_EQ(json::parse(run("promise", json{{"text", "x"}, {"kind", "sleep"}, {"target", 5}}))["error"], "invalid");
    const std::string note = Agent::DealTools::deals_note(deals.week(kAnna, "UTC"));
    EXPECT_NE(note.find("Медитация (0 of 5 ticks of Meditation)"), std::string::npos) << note;
    EXPECT_EQ(json::parse(run("drop_promise", json{{"id", made["promised"]}}))["dropped"], true);
    EXPECT_TRUE(Agent::DealTools::deals_note(deals.week(kAnna, "UTC")).empty());
}
