/**
 * @file test_now_day.cpp
 * @brief The day ribbon's own data (repositories/now/DayRepository.hpp): focus
 *        sessions and workouts of the local day (a workout still running has
 *        no end), tasks with a time on that day, in the person's zone.
 */

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/now/DayRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "test_helpers.hpp"

namespace {

using nlohmann::json;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaad0a1";

class NowDay : public TestHelpers::CoreBackedTest {
protected:
    Repositories::Now::DayRepository day;
    std::string config_file_name() const override { return "now_day_test_config.json"; }
    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec_params("DELETE FROM focus_sessions WHERE owner_id = $1::uuid", std::string(kAnna));
            txn.exec_params("DELETE FROM workout_sessions WHERE owner_id = $1::uuid", std::string(kAnna));
            txn.exec_params("DELETE FROM task_items WHERE owner_id = $1::uuid", std::string(kAnna));
            return true;
        });
    }
};

}  // namespace

TEST_F(NowDay, RibbonOpenEnds) {
    // 08:30-09:00 in Tashkent (UTC+5) and one at 23:50 the day before.
    day.add_focus(kAnna, "2026-10-29T03:30:00Z", "2026-10-29T04:00:00Z", 30, "Calendar");
    day.add_focus(kAnna, "2026-10-28T18:50:00Z", "2026-10-28T19:00:00Z", 10, "");
    Database::get().execute_write([](auto& txn) {
        txn.exec_params(
            "INSERT INTO workout_sessions (owner_id, name, started_at) VALUES ($1::uuid, 'Gym', now() - interval '20 "
            "minutes')",
            std::string(kAnna));
        txn.exec_params(
            "INSERT INTO task_items (owner_id, title, area, due, due_time) VALUES ($1::uuid, 'Pay rent', 'finance', "
            "'2026-10-29', '13:00'), ($1::uuid, 'No time', 'finance', '2026-10-29', NULL)",
            std::string(kAnna));
        return true;
    });
    const json d = day.day(kAnna, "2026-10-29", "Asia/Tashkent");
    ASSERT_EQ(d["focus"].size(), 1u) << "the one of the evening before is not this day";
    EXPECT_EQ(d["focus"][0]["start"], "2026-10-29T03:30:00Z");
    EXPECT_EQ(d["focus"][0]["label"], "Calendar");
    ASSERT_EQ(d["tasks"].size(), 1u) << "only tasks with a time";
    EXPECT_EQ(d["tasks"][0]["time"], "13:00");
    const json today = day.day(kAnna,
                               Database::get().execute_read([](auto& txn) {
                                   return txn
                                       .exec(
                                           "SELECT to_char((now() AT TIME ZONE 'Asia/Tashkent')::date, "
                                           "'YYYY-MM-DD')")[0][0]
                                       .template as<std::string>();
                               }),
                               "Asia/Tashkent");
    ASSERT_EQ(today["workouts"].size(), 1u);
    EXPECT_TRUE(today["workouts"][0]["end"].is_null()) << "a running workout has no end yet";
}

TEST_F(NowDay, FocusSessionValidates) {
    EXPECT_ANY_THROW(day.add_focus(kAnna, "2026-10-29T04:00:00Z", "2026-10-29T03:30:00Z", 30, ""));
    EXPECT_ANY_THROW(day.add_focus(kAnna, "2026-10-29T03:00:00Z", "2026-10-29T08:00:00Z", 300, ""));
}

TEST_F(NowDay, FocusSessionOnceFromTwoTabs) {
    const auto a = day.add_focus(kAnna, "2026-10-29T03:00:00Z", "2026-10-29T03:25:00Z", 25, "");
    const auto b = day.add_focus(kAnna, "2026-10-29T03:00:00Z", "2026-10-29T03:25:00Z", 25, "");
    EXPECT_EQ(a, b);
    const auto n = Database::get().execute_read([](auto& txn) {
        return txn.exec("SELECT count(*) FROM focus_sessions WHERE started_at = '2026-10-29T03:00:00Z'")[0][0]
            .template as<int>();
    });
    EXPECT_EQ(n, 1);
}

TEST_F(NowDay, NoTimeOnATaskWithoutADay) {
    const std::string id = Database::get().execute_write([](auto& txn) {
        return txn
            .exec_params(
                "INSERT INTO task_items (owner_id, title, area) VALUES ($1::uuid, 'Undated', 'finance') RETURNING "
                "id::text",
                std::string(kAnna))[0][0]
            .template as<std::string>();
    });
    Repositories::Tasks::TaskRepository::Patch p;
    p.due_time = std::optional<std::string>("10:00");
    Repositories::Tasks::TaskRepository().update(kAnna, id, p);
    const bool none = Database::get().execute_read([&](auto& txn) {
        return txn.exec_params("SELECT due_time IS NULL FROM task_items WHERE id = $1::uuid", id)[0][0]
            .template as<bool>();
    });
    EXPECT_TRUE(none) << "a time alone does not stick to a task without a day";
}

TEST_F(NowDay, TaskTimeGoesWithItsDay) {
    Database::get().execute_write([](auto& txn) {
        txn.exec_params(
            "INSERT INTO task_items (owner_id, title, area, due, due_time) VALUES ($1::uuid, 'T', 'finance', "
            "'2026-10-29', '09:15')",
            std::string(kAnna));
        return true;
    });
    EXPECT_EQ(day.day(kAnna, "2026-10-29", "UTC")["tasks"][0]["time"], "09:15");
}
