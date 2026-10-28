/**
 * @file test_ani_reminders.cpp
 * @brief Ani's recurring reminders: the next time in the reminder's own zone
 *        and weekdays (DST included), a due reminder becomes one remind item
 *        and moves on, and a long pause catches up once, not in bulk.
 */

#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/Reminders.hpp"
#include "database/Database.hpp"
#include "repositories/agent/InboxRepository.hpp"
#include "repositories/agent/ReminderRepository.hpp"
#include "test_helpers.hpp"

namespace {

using nlohmann::json;
using Repositories::Agent::InboxRepository;
using Repositories::Agent::ReminderRepository;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaacd";

class AniReminders : public TestHelpers::CoreBackedTest {
protected:
    ReminderRepository reminders;

    std::string config_file_name() const override { return "ani_reminders_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("DELETE FROM ani_reminders");
            txn.exec("DELETE FROM ani_inbox");
            return true;
        });
    }

    static std::string next(int days, const std::string& at, const std::string& tz, const std::string& after) {
        return Database::get().execute_read([&](auto& txn) {
            return txn
                .exec_params("SELECT to_char(" + ReminderRepository::next_sql("$1", "$2", "$3", "$4::timestamptz") +
                                 " AT TIME ZONE 'UTC', 'YYYY-MM-DD HH24:MI')",
                             days,
                             at,
                             tz,
                             after)[0][0]
                .template as<std::string>();
        });
    }

    static void sql(const std::string& q, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(q, id);
            return true;
        });
    }
};

}  // namespace

TEST_F(AniReminders, NextAtInZone) {
    // Wednesday 10:00 in Almaty, weekdays at 09:00: Thursday 09:00 local = 04:00 UTC.
    EXPECT_EQ(next(31, "09:00", "Asia/Almaty", "2026-10-28 10:00:00+05"), "2026-10-29 04:00");
    // Berlin leaves summer time on Sunday 2026-10-25: 09:00 local is 08:00 UTC that day.
    EXPECT_EQ(next(64, "09:00", "Europe/Berlin", "2026-10-24 12:00:00+00"), "2026-10-25 08:00");
}

TEST_F(AniReminders, NextAtDaysMask) {
    // Saturday: the next weekday is Monday.
    EXPECT_EQ(next(31, "09:00", "Asia/Almaty", "2026-10-31 10:00:00+05"), "2026-11-02 04:00");
    // Later the same day.
    EXPECT_EQ(next(127, "21:30", "Asia/Almaty", "2026-10-28 10:00:00+05"), "2026-10-28 16:30");
}

TEST_F(AniReminders, DueAddsRemind) {
    const auto made = reminders.add(kAnna, "Выпей воды", "09:00", 127, "Asia/Almaty");
    ASSERT_TRUE(made.has_value());
    sql("UPDATE ani_reminders SET next_at = now() - interval '1 second' WHERE id::text = $1", (*made)["id"]);
    Agent::Reminders::process_due(json::object());
    const json items = InboxRepository().claim(kAnna, 5);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0]["kind"], "remind");
    EXPECT_EQ(items[0]["say"], "Выпей воды");
    EXPECT_EQ(items[0]["payload"]["via"], "schedule");
    const bool moved = Database::get().execute_read([&](auto& txn) {
        return txn
            .exec_params("SELECT next_at > now() FROM ani_reminders WHERE id::text = $1",
                         (*made)["id"].get<std::string>())[0][0]
            .template as<bool>();
    });
    EXPECT_TRUE(moved);
}

TEST_F(AniReminders, CatchUpOnce) {
    const auto made = reminders.add(kAnna, "Разминка", "09:00", 127, "Asia/Almaty");
    ASSERT_TRUE(made.has_value());
    sql("UPDATE ani_reminders SET next_at = now() - interval '5 days' WHERE id::text = $1", (*made)["id"]);
    Agent::Reminders::process_due(json::object());
    Agent::Reminders::process_due(json::object());
    EXPECT_EQ(InboxRepository().claim(kAnna, 10).size(), 1u) << "five missed days say it once";
}

TEST_F(AniReminders, AtMostTwenty) {
    for (int i = 0; i < ReminderRepository::kMaxPerUser; ++i)
        ASSERT_TRUE(reminders.add(kAnna, "r", "09:00", 1, "UTC").has_value());
    EXPECT_FALSE(reminders.add(kAnna, "r", "09:00", 1, "UTC").has_value());
}
