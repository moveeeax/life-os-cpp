/**
 * @file test_tasks_parse_job.cpp
 * @brief The tasks_parse job with a scripted provider and a real database:
 *        what the request carries, the duplicate flag, a refused area, and
 *        the fallback to the money provider with the tasks prompt.
 */

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "jobs/BuiltinHandlers.hpp"
#include "jobs/Dispatcher.hpp"
#include "jobs/TasksParseHandler.hpp"
#include "net/Http.hpp"
#include "repositories/tasks/ParseJobRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaab3";

class ScriptedProvider : public Net::Http::Transport {
public:
    std::vector<Net::Http::Response> replies;
    std::vector<json> bodies;
    std::vector<std::string> urls;

    Net::Http::Response get(const std::string&, const std::vector<std::pair<std::string, std::string>>&) override {
        throw std::runtime_error("ScriptedProvider: GET is not expected");
    }

    Net::Http::Response post_json(const std::string& url,
                                  const std::string& body,
                                  const std::vector<std::pair<std::string, std::string>>&,
                                  long) override {
        urls.push_back(url);
        bodies.push_back(json::parse(body, nullptr, false));
        if (replies.empty()) {
            throw std::runtime_error("ScriptedProvider: unexpected request");
        }
        auto r = replies.front();
        replies.erase(replies.begin());
        return r;
    }
};

std::string completion(const json& content) {
    return json{{"model", "gpt-6-luna-2026"},
                {"choices", json::array({json{{"message", json{{"role", "assistant"}, {"content", content.dump()}}}}})},
                {"usage", json{{"prompt_tokens", 120}, {"completion_tokens", 40}}}}
        .dump();
}

class TasksParseJobTest : public TestHelpers::CoreBackedTest {
protected:
    ScriptedProvider provider;
    Repositories::Tasks::ParseJobRepository jobs;

    std::string config_file_name() const override { return "tasks_parse_job_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override {
        cfg["tasks"]["enabled"] = true;
        cfg["tasks"]["llm"]["base_url"] = "https://tasks-llm.example/v1";
        cfg["tasks"]["llm"]["api_key"] = "sk-tasks";
        cfg["tasks"]["llm"]["model"] = "gpt-6-luna";
        cfg["tasks"]["llm"]["prompt_parse"] = "Answer with task JSON.";
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Net::Http::install_for_testing(&provider);
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE notes, task_parse_jobs, task_notes, task_items");
            txn.exec_params(
                "INSERT INTO users (id, email, confirmed, role_id) "
                "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) ON CONFLICT DO NOTHING",
                std::string(kAnna),
                std::string(kAnna) + "@example.test");
            return true;
        });
    }

    void TearDown() override {
        Net::Http::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    std::string job(const std::string& text) {
        return jobs.create(kAnna, text, "2026-10-07", std::nullopt)["id"].get<std::string>();
    }

    json run(const std::string& id) {
        return Jobs::TasksParse::process_job(json{{"job_id", id}, {"owner_id", kAnna}, {"max_attempts", 3}});
    }
};

class TasksParseFallbackTest : public TasksParseJobTest {
protected:
    std::string config_file_name() const override { return "tasks_parse_fallback_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override {
        cfg["tasks"]["enabled"] = true;
        cfg["tasks"]["llm"]["prompt_parse"] = "Answer with task JSON.";
        cfg["money"]["llm"]["base_url"] = "https://money-llm.example/v1";
        cfg["money"]["llm"]["api_key"] = "sk-money";
        cfg["money"]["llm"]["model"] = "gpt-6-luna";
        cfg["money"]["llm"]["prompt_parse"] = "Answer with ledger JSON.";
    }
};

}  // namespace

TEST_F(TasksParseJobTest, TwoLinesAndAnOpenDuplicate) {
    Repositories::Tasks::TaskRepository::Input open;
    open.title = "Call parents";
    open.area = "relationships";
    Repositories::Tasks::TaskRepository().create(kAnna, open);
    provider.replies.push_back(
        {200,
         completion(json{{"lines",
                          {{{"title", "Buy water"}, {"area", "projects"}, {"effort", "5min"}, {"due", "2026-10-08"}},
                           {{"title", "Call parents"}, {"area", "relationships"}}}}})});
    const std::string id = job("buy water tomorrow and call parents");
    EXPECT_EQ(run(id)["status"], "done");

    ASSERT_EQ(provider.bodies.size(), 1u);
    EXPECT_EQ(provider.bodies[0]["messages"][0]["content"], "Answer with task JSON.");
    const json user = json::parse(provider.bodies[0]["messages"][1]["content"].get<std::string>());
    EXPECT_EQ(user["hint_date"], "2026-10-07");
    EXPECT_EQ(user["areas"].size(), 6u);
    EXPECT_EQ(user["text"], "buy water tomorrow and call parents");

    const json row = *jobs.get(kAnna, id);
    ASSERT_EQ(row["result"].size(), 2u);
    EXPECT_EQ(row["result"][0]["due"], "2026-10-08");
    EXPECT_FALSE(row["result"][0]["possible_duplicate"]);
    EXPECT_TRUE(row["result"][1]["possible_duplicate"]) << "the same title is open already";
}

TEST_F(TasksParseJobTest, AWorkAreaFailsTheAnswer) {
    provider.replies.push_back({200, completion(json{{"lines", {{{"title", "Deploy"}, {"area", "work"}}}}})});
    const std::string id = job("deploy the release");
    EXPECT_EQ(run(id)["status"], "failed");
    const json row = *jobs.get(kAnna, id);
    EXPECT_EQ(row["status"], "failed");
    EXPECT_EQ(row["error"].get<std::string>().rfind("invalid_answer", 0), 0u);
}

TEST_F(TasksParseFallbackTest, UsesTheMoneyProviderWithTheTasksPrompt) {
    provider.replies.push_back({200, completion(json{{"lines", {{{"title", "Buy water"}, {"area", "projects"}}}}})});
    const std::string id = job("buy water");
    EXPECT_EQ(run(id)["status"], "done");
    ASSERT_EQ(provider.urls.size(), 1u);
    EXPECT_EQ(provider.urls[0].rfind("https://money-llm.example/v1", 0), 0u);
    EXPECT_EQ(provider.bodies[0]["messages"][0]["content"], "Answer with task JSON.");
}

TEST_F(TasksParseJobTest, ARedeliveryOfARunningRowWaitsUntilTheRowIsStale) {
    const std::string id = job("buy water");
    ASSERT_EQ(jobs.start(id), 1) << "a worker claimed the row and died";
    try {
        run(id);
        ADD_FAILURE() << "a row another delivery holds is not run twice";
    } catch (const Jobs::DeferJob& e) {
        // Deferred, not failed: the queue does not count an attempt, so a worker
        // restart cannot spend the whole retry budget while the row is held.
        EXPECT_GT(e.delay_ms, 9 * 60 * 1000) << "the row was claimed a moment ago";
        EXPECT_LE(e.delay_ms, 10 * 60 * 1000 + 2000) << "the stale window plus a second";
    }
    EXPECT_TRUE(provider.bodies.empty());
    EXPECT_EQ((*jobs.get(kAnna, id))["status"], "running");
}

TEST_F(TasksParseJobTest, AStaleRunningRowReadsAsFailedUntilItIsClaimedAgain) {
    const std::string id = job("buy water");
    ASSERT_EQ(jobs.start(id), 1);
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("UPDATE task_parse_jobs SET started_at = now() - interval '11 minutes' WHERE id = $1::uuid",
                        id);
        return true;
    });
    const json polled = *jobs.get(kAnna, id);
    EXPECT_EQ(polled["status"], "failed") << "a page polling a row whose worker died must stop waiting";
    EXPECT_EQ(polled["error"].get<std::string>().rfind("worker_lost", 0), 0u) << polled.dump();
    EXPECT_EQ(jobs.stale_in_ms(id), 0) << "claimable now";

    provider.replies.push_back({200, completion(json{{"lines", {{{"title", "Buy water"}, {"area", "projects"}}}}})});
    EXPECT_EQ(run(id)["status"], "done") << "the queue's redelivery still finishes it";
    EXPECT_EQ((*jobs.get(kAnna, id))["status"], "done");
}

TEST_F(TasksParseJobTest, OnlyTheCurrentClaimWritesTheRow) {
    const std::string id = job("buy water");
    const int first = jobs.start(id);
    ASSERT_EQ(first, 1);
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("UPDATE task_parse_jobs SET started_at = now() - interval '11 minutes' WHERE id = $1::uuid",
                        id);
        return true;
    });
    const int second = jobs.start(id);
    ASSERT_EQ(second, 2) << "the stale row was claimed again";

    // The first worker wakes up while the second still runs: none of its writes land.
    jobs.finish(id, first, json::array(), "late-model", 1, 1);
    jobs.requeue(id, first);
    jobs.fail(id, "internal_error", "late", first);
    json row = *jobs.get(kAnna, id);
    EXPECT_EQ(row["status"], "running");
    EXPECT_TRUE(row["result"].is_null());

    jobs.finish(id, second, json::array({json{{"title", "Buy water"}, {"area", "projects"}}}), "gpt-6-luna", 1, 1);
    row = *jobs.get(kAnna, id);
    EXPECT_EQ(row["status"], "done");
    EXPECT_EQ(row["result"].size(), 1u);
}

TEST_F(TasksParseJobTest, AFailBeforeTheClaimClosesAQueuedRow) {
    const std::string id = job("buy water");
    jobs.fail(id, "tasks_disabled", "the module is off");
    EXPECT_EQ((*jobs.get(kAnna, id))["status"], "failed");
}

TEST_F(TasksParseJobTest, AQueuedRowTheQueueLostReadsAsFailedUntilADeliveryClaimsIt) {
    const std::string id = job("buy water");
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("UPDATE task_parse_jobs SET created_at = now() - interval '11 minutes' WHERE id = $1::uuid",
                        id);
        return true;
    });
    const json polled = *jobs.get(kAnna, id);
    EXPECT_EQ(polled["status"], "failed") << "a page polling a row the queue lost must stop waiting";
    EXPECT_EQ(polled["error"].get<std::string>().rfind("queue_lost", 0), 0u) << polled.dump();

    // The row stayed queued underneath: a late delivery still claims and finishes it.
    provider.replies.push_back({200, completion(json{{"lines", {{{"title", "Buy water"}, {"area", "projects"}}}}})});
    EXPECT_EQ(run(id)["status"], "done");
    EXPECT_EQ((*jobs.get(kAnna, id))["status"], "done");
}

TEST_F(TasksParseJobTest, AFailBeforeTheClaimLeavesALiveRunningRowAlone) {
    const std::string id = job("buy water");
    ASSERT_EQ(jobs.start(id), 1) << "a worker holds the row";

    // The queue gave up on a redelivery while the first worker still runs the
    // row (its provider call is slow, not dead): that worker's own result must land.
    jobs.fail(id, "queue_gave_up", "visibility timeout exceeded");
    EXPECT_EQ((*jobs.get(kAnna, id))["status"], "running");
    jobs.finish(
        id, /*attempt=*/1, json::array({json{{"title", "Buy water"}, {"area", "projects"}}}), "gpt-6-luna", 1, 1);
    EXPECT_EQ((*jobs.get(kAnna, id))["status"], "done");

    // A running row past the stale window has no live worker: the same fail closes it.
    const std::string stale = job("call parents");
    ASSERT_EQ(jobs.start(stale), 1);
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("UPDATE task_parse_jobs SET started_at = now() - interval '11 minutes' WHERE id = $1::uuid",
                        stale);
        return true;
    });
    jobs.fail(stale, "queue_gave_up", "visibility timeout exceeded");
    const json row = *jobs.get(kAnna, stale);
    EXPECT_EQ(row["status"], "failed");
    EXPECT_EQ(row["error"].get<std::string>().rfind("queue_gave_up", 0), 0u) << row.dump();
}

TEST_F(TasksParseJobTest, AQueuedRowTheQueueLostDoesNotHoldAParseSlot) {
    for (int i = 0; i < 3; ++i) {
        job("buy water");
    }
    EXPECT_EQ(jobs.open_count(kAnna), 3);
    Database::get().execute_write([](auto& txn) {
        txn.exec("UPDATE task_parse_jobs SET created_at = now() - interval '11 minutes'");
        return true;
    });
    EXPECT_EQ(jobs.open_count(kAnna), 0) << "queued rows nobody picked for the whole window must not lock the user out";
    job("call parents");
    EXPECT_EQ(jobs.open_count(kAnna), 1);
}

TEST_F(TasksParseJobTest, TheQueueGivingUpFailsTheRow) {
    Jobs::register_builtin_handlers();
    const std::string id = job("buy water");
    Jobs::Job dead;
    dead.id = "redis-job";
    dead.type = Jobs::TasksParse::kJobType;
    dead.payload = json{{"job_id", id}, {"owner_id", kAnna}};
    Jobs::Dispatcher::get().on_dead(dead, "visibility timeout exceeded");
    const json row = *jobs.get(kAnna, id);
    EXPECT_EQ(row["status"], "failed");
    EXPECT_EQ(row["error"].get<std::string>().rfind("queue_gave_up: ", 0), 0u) << row.dump();

    // A finished row stays as it is.
    provider.replies.push_back({200, completion(json{{"lines", {{{"title", "Buy water"}, {"area", "projects"}}}}})});
    const std::string done = job("buy water");
    ASSERT_EQ(run(done)["status"], "done");
    dead.payload["job_id"] = done;
    Jobs::Dispatcher::get().on_dead(dead, "late");
    EXPECT_EQ((*jobs.get(kAnna, done))["status"], "done");
}

TEST_F(TasksParseJobTest, AStaleRunningRowDoesNotHoldAParseSlot) {
    for (int i = 0; i < 3; ++i) {
        job("buy water");
    }
    EXPECT_EQ(jobs.open_count(kAnna), 3);
    Database::get().execute_write([](auto& txn) {
        txn.exec("UPDATE task_parse_jobs SET status = 'running', started_at = now() - interval '11 minutes'");
        return true;
    });
    EXPECT_EQ(jobs.open_count(kAnna), 0) << "rows a dead worker left must not lock the user out";
    ASSERT_EQ(jobs.start(job("call parents")), 1);
    EXPECT_EQ(jobs.open_count(kAnna), 1) << "a fresh running row still counts";
}

TEST_F(TasksParseJobTest, ALateResultDoesNotOverwriteAFinishedRow) {
    provider.replies.push_back({200, completion(json{{"lines", {{{"title", "Buy water"}, {"area", "projects"}}}}})});
    const std::string id = job("buy water");
    ASSERT_EQ(run(id)["status"], "done");
    jobs.finish(id, /*attempt=*/1, json::array(), "late-model", 1, 1);
    jobs.fail(id, "internal_error", "a worker whose stale row was claimed again");
    const json row = *jobs.get(kAnna, id);
    EXPECT_EQ(row["status"], "done");
    EXPECT_EQ(row["result"].size(), 1u);
    EXPECT_EQ(row["model"], "gpt-6-luna-2026");
}
