/**
 * @file test_tasks_repositories.cpp
 * @brief The tasks repositories against a real database: completed_at set on
 *        close and cleared on reopen, the agenda day read in the caller's zone,
 *        the source pair and the unique external_id, the owner scope.
 */

#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/tasks/Errors.hpp"
#include "repositories/tasks/NoteRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "test_helpers.hpp"

namespace {

using json = nlohmann::json;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaab1";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbc1";

class TasksRepositoryTest : public TestHelpers::CoreBackedTest {
protected:
    std::string config_file_name() const override { return "tasks_repositories_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE task_parse_jobs, task_notes, task_items");
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
};

}  // namespace

TEST_F(TasksRepositoryTest, ClosingSetsAndReopeningClearsCompletedAt) {
    Repositories::Tasks::TaskRepository repo;
    Repositories::Tasks::TaskRepository::Input in;
    in.title = "Call parents";
    in.area = "relationships";
    const json t = repo.create(kAnna, in);
    EXPECT_TRUE(t["completed_at"].is_null());
    Repositories::Tasks::TaskRepository::Patch close;
    close.status = "done";
    const json done = repo.update(kAnna, t["id"], close);
    EXPECT_TRUE(done["completed_at"].is_string());
    Repositories::Tasks::TaskRepository::Patch reopen;
    reopen.status = "open";
    EXPECT_TRUE(repo.update(kAnna, t["id"], reopen)["completed_at"].is_null());
    EXPECT_FALSE(repo.get(kBoris, t["id"]).has_value()) << "another owner's task is not found";
}

TEST_F(TasksRepositoryTest, AgendaItemsReadTheDayInTheCallersZone) {
    Repositories::Tasks::TaskRepository repo;
    Repositories::Tasks::TaskRepository::Input in;
    in.title = "Closed late at night";
    in.area = "projects";
    const json t = repo.create(kAnna, in);
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params(
            "UPDATE task_items SET status = 'done', completed_at = '2026-10-06 23:30:00+00' WHERE id = $1::uuid",
            t["id"].get<std::string>());
        return true;
    });
    const auto bkk = repo.agenda_items(kAnna, "2026-10-07", "Asia/Bangkok");
    ASSERT_EQ(bkk.size(), 1u);
    EXPECT_EQ(bkk[0].completed_on, "2026-10-07") << "06:30 on the 7th in Bangkok";
    EXPECT_EQ(repo.agenda_items(kAnna, "2026-10-07", "UTC")[0].completed_on, "2026-10-06");
    EXPECT_TRUE(Repositories::Tasks::TaskRepository::is_timezone("Asia/Bangkok"));
    EXPECT_FALSE(Repositories::Tasks::TaskRepository::is_timezone("Mars/Olympus"));
}

TEST_F(TasksRepositoryTest, TheSourceNeedsItsRefAndExternalIdIsUnique) {
    Repositories::Tasks::TaskRepository repo;
    Repositories::Tasks::TaskRepository::Input in;
    in.title = "x";
    in.area = "finance";
    in.source_kind = "url";
    EXPECT_THROW(repo.create(kAnna, in), Repositories::Tasks::Invariant) << "a kind without a ref";
    in.source_ref = "https://example.com/mail/1";
    in.external_id = "notion-1";
    repo.create(kAnna, in);
    EXPECT_THROW(repo.create(kAnna, in), Repositories::Tasks::Duplicate);
}

TEST_F(TasksRepositoryTest, ANoteBelongsToItsOwner) {
    Repositories::Tasks::NoteRepository notes;
    const json n = notes.create(kAnna, "Coworking day passes");
    EXPECT_EQ(n["status"], "inbox");
    EXPECT_EQ(notes.list(kAnna, "inbox").size(), 1u);
    EXPECT_EQ(notes.list(kBoris, "inbox").size(), 0u);
    EXPECT_THROW(notes.update(kBoris, n["id"], std::nullopt, std::string("archived")), Repositories::Tasks::NotFound);
    EXPECT_EQ(notes.update(kAnna, n["id"], std::nullopt, std::string("archived"))["status"], "archived");
}
