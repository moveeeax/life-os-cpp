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
            txn.exec("TRUNCATE TABLE task_parse_jobs, task_notes, task_items");
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
