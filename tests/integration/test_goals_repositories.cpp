/**
 * @file test_goals_repositories.cpp
 * @brief The goals repository against a real database: the kind CHECKs, one
 *        check-in per date, sections only on a steps goal, a binary result
 *        closing the goal, deleting a goal unlinking its tasks, the owner scope.
 */

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/goals/Errors.hpp"
#include "repositories/goals/GoalRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "test_helpers.hpp"

namespace {

using json = nlohmann::json;
using Repositories::Goals::GoalRepository;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaab4";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbc4";

class GoalsRepositoryTest : public TestHelpers::CoreBackedTest {
protected:
    GoalRepository goals;

    std::string config_file_name() const override { return "goals_repositories_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE task_parse_jobs, task_notes, task_items, goal_items CASCADE");
            for (const char* id : {kAnna, kBoris}) {
                txn.exec_params(
                    "INSERT INTO users (id, email, confirmed, role_id) "
                    "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                    "ON CONFLICT DO NOTHING",
                    std::string(id),
                    std::string(id) + "@example.test");
            }
            return true;
        });
    }

    static GoalRepository::Input input(const char* kind) {
        GoalRepository::Input in;
        in.title = "A goal";
        in.area = "growth";
        in.kind = kind;
        in.start_date = "2026-09-01";
        in.due = "2026-12-31";
        return in;
    }
};

}  // namespace

TEST_F(GoalsRepositoryTest, EachKindNeedsItsOwnFields) {
    auto number = input("number");
    EXPECT_THROW(goals.create(kAnna, number), Repositories::Goals::Invariant) << "no start and target";
    number.start_value = 93;
    number.target_value = 75;
    number.unit = "kg";
    EXPECT_EQ(goals.create(kAnna, number)["kind"], "number");
    auto count = input("count");
    EXPECT_THROW(goals.create(kAnna, count), Repositories::Goals::Invariant) << "no target count";
    count.target_count = 12;
    EXPECT_EQ(goals.create(kAnna, count)["target_count"], 12);
    auto steps = input("steps");
    steps.unit = "kg";
    EXPECT_THROW(goals.create(kAnna, steps), Repositories::Goals::Invariant) << "a unit only on a number goal";
    auto backwards = input("binary");
    backwards.due = "2026-08-01";
    EXPECT_THROW(goals.create(kAnna, backwards), Repositories::Goals::Invariant) << "due before the start";
}

TEST_F(GoalsRepositoryTest, OneCheckinPerDateAndOnlyOnANumberGoal) {
    auto in = input("number");
    in.start_value = 93;
    in.target_value = 75;
    const std::string id = goals.create(kAnna, in)["id"];
    goals.put_checkin(kAnna, id, "2026-10-06", 91.6, "");
    const json again = goals.put_checkin(kAnna, id, "2026-10-06", 91.4, "after the run");
    EXPECT_DOUBLE_EQ(again["value"].get<double>(), 91.4);
    const auto points = goals.checkin_points(kAnna, id);
    ASSERT_EQ(points.size(), 1u) << "the same date replaced";
    EXPECT_THROW(goals.put_checkin(kBoris, id, "2026-10-07", 90, ""), Repositories::Goals::NotFound);
    const std::string steps = goals.create(kAnna, input("steps"))["id"];
    EXPECT_THROW(goals.put_checkin(kAnna, steps, "2026-10-07", 1, ""), Repositories::Goals::Invariant);
    EXPECT_THROW(goals.add_section(kAnna, id, "Section", std::nullopt), Repositories::Goals::Invariant)
        << "sections belong to a steps goal";
}

TEST_F(GoalsRepositoryTest, ABinaryResultClosesTheGoal) {
    const std::string id = goals.create(kAnna, input("binary"))["id"];
    GoalRepository::Patch p;
    p.result = "pass";
    const json closed = goals.update(kAnna, id, p);
    EXPECT_EQ(closed["status"], "done");
    EXPECT_EQ(closed["result"], "pass");
    EXPECT_TRUE(closed["completed_at"].is_string());
    GoalRepository::Patch reopen;
    reopen.status = "active";
    EXPECT_TRUE(goals.update(kAnna, id, reopen)["completed_at"].is_null());
    EXPECT_FALSE(goals.get(kBoris, id).has_value());
}

TEST_F(GoalsRepositoryTest, DeletingAGoalUnlinksItsTasks) {
    const std::string id = goals.create(kAnna, input("steps"))["id"];
    const std::string section = goals.add_section(kAnna, id, "Lectures", std::nullopt)["id"];
    Repositories::Tasks::TaskRepository tasks;
    Repositories::Tasks::TaskRepository::Input t;
    t.title = "Lectures 1-14";
    t.area = "growth";
    t.status = "in_progress";
    t.goal_id = id;
    t.goal_section_id = section;
    const json task = tasks.create(kAnna, t);
    EXPECT_EQ(task["goal_title"], "A goal");
    const auto counts = goals.task_counts(kAnna);
    ASSERT_EQ(counts.count(id), 1u);
    EXPECT_EQ(counts.at(id).total, 1);
    EXPECT_EQ(counts.at(id).done, 0);
    EXPECT_EQ(goals.tasks_of(kAnna, id).size(), 1u);
    goals.remove(kAnna, id);
    const json after = *tasks.get(kAnna, task["id"]);
    EXPECT_TRUE(after["goal_id"].is_null());
    EXPECT_TRUE(after["goal_section_id"].is_null());
    EXPECT_EQ(after["status"], "in_progress");
}
