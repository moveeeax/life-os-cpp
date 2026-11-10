/**
 * @file test_unified_parse_job.cpp
 * @brief The unified_parse job with a scripted provider and a real database:
 *        one phrase, one model call, the rows of each kind filed as finished
 *        jobs of their modules, and with apply the rows created once.
 */

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "core/JobHandlers.hpp"
#include "database/Database.hpp"
#include "jobs/Dispatcher.hpp"
#include "net/Http.hpp"
#include "parse/jobs/ParseJob.hpp"
#include "repositories/food/ParseJobRepository.hpp"
#include "repositories/food/WaterRepository.hpp"
#include "repositories/money/AccountRepository.hpp"
#include "repositories/money/CategoryRepository.hpp"
#include "repositories/money/CurrencyRepository.hpp"
#include "repositories/money/ParseJobRepository.hpp"
#include "repositories/parse/ParseJobRepository.hpp"
#include "repositories/tasks/ParseJobRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaab1";
constexpr const char* kPhrase = "кофе 1200 и творог 200 г, завтра позвонить маме";

class ScriptedProvider : public Net::Http::Transport {
public:
    std::vector<Net::Http::Response> replies;
    std::vector<std::string> urls;
    std::vector<json> bodies;

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
                {"usage", json{{"prompt_tokens", 900}, {"completion_tokens", 120}}}}
        .dump();
}

long count(const std::string& sql) {
    return Database::get().execute_read(
        [&](auto& txn) { return txn.exec_params(sql, std::string(kAnna))[0][0].template as<long>(); });
}

class UnifiedParseJobTest : public TestHelpers::CoreBackedTest {
protected:
    ScriptedProvider provider;
    Repositories::UnifiedParse::ParseJobRepository jobs;
    json kaspi, food_category;

    std::string config_file_name() const override { return "unified_parse_job_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override {
        cfg["food"]["enabled"] = true;
        cfg["food"]["llm"]["base_url"] = "https://food-llm.example/v1";
        cfg["food"]["llm"]["api_key"] = "sk-food";
        cfg["food"]["llm"]["model"] = "gpt-6-food";
        cfg["food"]["llm"]["prompt"] = "FOOD RULES";
        cfg["food"]["llm"]["max_tokens"] = 1500;
        cfg["money"]["enabled"] = true;
        cfg["money"]["llm"]["base_url"] = "https://money-llm.example/v1";
        cfg["money"]["llm"]["api_key"] = "sk-money";
        cfg["money"]["llm"]["model"] = "gpt-6-luna";
        cfg["money"]["llm"]["prompt_parse"] = "MONEY RULES";
        cfg["money"]["llm"]["max_tokens"] = 2500;
        cfg["tasks"]["enabled"] = true;
        cfg["tasks"]["llm"]["prompt_parse"] = "TASKS RULES";
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Net::Http::install_for_testing(&provider);
        Database::get().execute_write([](auto& txn) {
            txn.exec(
                "TRUNCATE TABLE parse_jobs, food_parse_jobs, food_entries, food_items, food_goals, food_water, "
                "money_parse_jobs, money_merchants, money_transfers, money_transactions, money_categories, "
                "money_accounts, money_currencies, task_parse_jobs, task_notes, task_items CASCADE");
            txn.exec_params(
                "INSERT INTO users (id, email, confirmed, role_id) "
                "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) ON CONFLICT DO NOTHING",
                std::string(kAnna),
                std::string(kAnna) + "@example.test");
            return true;
        });
        Repositories::Money::CurrencyRepository().seed_defaults(kAnna);
        Repositories::Money::AccountRepository::Input a;
        a.name = "Kaspi Gold";
        a.bank = "Kaspi";
        a.currency = "KZT";
        a.last4 = "8880";
        kaspi = Repositories::Money::AccountRepository().create(kAnna, a);
        Repositories::Money::CategoryRepository::Input c;
        c.name = "Cafe";
        food_category = Repositories::Money::CategoryRepository().create(kAnna, c);
    }

    void TearDown() override {
        Net::Http::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    std::string job(bool apply, const std::string& meal = "breakfast") {
        Repositories::UnifiedParse::ParseJobRepository::Apply a;
        a.apply = apply;
        bool replayed = false;
        return jobs.create(kAnna, kPhrase, meal, "2026-10-07", a, replayed, [](auto&, const std::string&) {})["id"]
            .get<std::string>();
    }

    json run(const std::string& id) {
        return Jobs::UnifiedParse::process_job(json{{"job_id", id}, {"owner_id", kAnna}, {"max_attempts", 3}});
    }

    json answer(const json& water = nullptr) const {
        return json{
            {"food",
             {{"lines",
               {{{"name", "Творог 5%"},
                 {"grams", 200},
                 {"kcal", 242},
                 {"protein_g", 34},
                 {"fat_g", 10},
                 {"carbs_g", 6}}}}}},
            {"money", {{"lines", {{{"amount", 1200}, {"name", "Кофе"}, {"category_id", food_category["id"]}}}}}},
            {"tasks", {{"lines", {{{"title", "Позвонить маме"}, {"area", "relationships"}, {"due", "2026-10-08"}}}}}},
            {"water", water}};
    }
};

}  // namespace

TEST_F(UnifiedParseJobTest, OnePhraseGivesASpendAFoodRowAndATaskFromOneCall) {
    provider.replies.push_back({200, completion(answer())});
    const std::string id = job(false);
    EXPECT_EQ(run(id)["status"], "done");

    ASSERT_EQ(provider.bodies.size(), 1u) << "one model call for the three kinds";
    EXPECT_EQ(provider.urls[0].rfind("https://money-llm.example/v1", 0), 0u) << "the money provider";
    const json& req = provider.bodies[0];
    EXPECT_EQ(req["model"], "gpt-6-luna");
    EXPECT_EQ(req["max_tokens"], 1500 + 2500 + 2500) << "the budgets of the three parses together";
    const std::string system = req["messages"][0]["content"].get<std::string>();
    for (const char* part :
         {"## food\n\nFOOD RULES", "## money\n\nMONEY RULES", "## tasks\n\nTASKS RULES", "\"water\""}) {
        EXPECT_NE(system.find(part), std::string::npos) << part;
    }
    const json user = json::parse(req["messages"][1]["content"].get<std::string>());
    EXPECT_EQ(user["text"], kPhrase);
    EXPECT_EQ(user["food"]["meal"], "breakfast");
    EXPECT_EQ(user["money"]["accounts"].size(), 1u);
    EXPECT_EQ(user["money"]["hint_date"], "2026-10-07");
    EXPECT_EQ(user["tasks"]["areas"].size(), 6u);

    const json row = *jobs.get(kAnna, id);
    EXPECT_EQ(row["status"], "done");
    EXPECT_EQ(row["model"], "gpt-6-luna-2026");
    ASSERT_EQ(row["result"]["food"].size(), 1u);
    ASSERT_EQ(row["result"]["money"].size(), 1u);
    ASSERT_EQ(row["result"]["tasks"].size(), 1u);
    EXPECT_TRUE(row["result"]["water"].is_null());
    EXPECT_TRUE(row["applied"].is_null()) << "nothing is created without apply";
    EXPECT_EQ(count("SELECT count(*) FROM money_transactions WHERE owner_id = $1::uuid"), 0);

    // Each kind is a finished job of its module: the module's accept route takes it.
    const json money_job =
        *Repositories::Money::ParseJobRepository().get(kAnna, row["result"]["jobs"]["money"].get<std::string>());
    EXPECT_EQ(money_job["status"], "done");
    EXPECT_EQ(money_job["result"], row["result"]["money"]);
    EXPECT_TRUE(money_job["accepted_at"].is_null());
    const json task_job =
        *Repositories::Tasks::ParseJobRepository().get(kAnna, row["result"]["jobs"]["tasks"].get<std::string>());
    EXPECT_EQ(task_job["result"][0]["title"], "Позвонить маме");
    const json food_job =
        *Repositories::ParseJobRepository().get(kAnna, row["result"]["jobs"]["food"].get<std::string>());
    EXPECT_EQ(food_job["meal"], "breakfast");
    EXPECT_EQ(food_job["result"][0]["grams"], 200);
    EXPECT_EQ(jobs.open_count(kAnna), 0);
    EXPECT_EQ(Repositories::Money::ParseJobRepository().open_count(kAnna), 0) << "no slot of the module parses";
}

TEST_F(UnifiedParseJobTest, ApplyCreatesEveryRowOnce) {
    provider.replies.push_back({200, completion(answer(json{{"ml", 500}}))});
    const std::string id = job(true, "lunch");
    EXPECT_EQ(run(id)["status"], "done");
    EXPECT_EQ(run(id)["status"], "done") << "a redelivery finds the job done";

    const json row = *jobs.get(kAnna, id);
    ASSERT_EQ(row["applied"]["money"].size(), 1u);
    EXPECT_EQ(row["applied"]["money"][0]["status"], "posted") << "the one open KZT account";
    ASSERT_EQ(row["applied"]["tasks"].size(), 1u);
    ASSERT_EQ(row["applied"]["food"].size(), 1u);
    EXPECT_EQ(row["applied"]["water"]["ml"], 500);

    EXPECT_EQ(count("SELECT count(*) FROM money_transactions WHERE owner_id = $1::uuid AND status = 'posted'"), 1);
    EXPECT_EQ(count("SELECT count(*) FROM task_items WHERE owner_id = $1::uuid AND status = 'open'"), 1);
    EXPECT_EQ(count("SELECT count(*) FROM food_entries WHERE owner_id = $1::uuid AND meal = 'lunch' AND "
                    "date = '2026-10-07'"),
              1);
    EXPECT_EQ(Repositories::WaterRepository().day(kAnna, "2026-10-07")["ml"], 500);
    const json money_job =
        *Repositories::Money::ParseJobRepository().get(kAnna, row["result"]["jobs"]["money"].get<std::string>());
    EXPECT_FALSE(money_job["accepted_at"].is_null()) << "the child job cannot be accepted again";
    EXPECT_EQ(money_job["applied"]["money"], row["applied"]["money"]);
}

TEST_F(UnifiedParseJobTest, APartThatBreaksItsRulesIsDroppedAndTheOthersStay) {
    json bad = answer();
    bad["tasks"]["lines"][0]["area"] = "work";
    provider.replies.push_back({200, completion(bad)});
    const std::string id = job(false);
    EXPECT_EQ(run(id)["status"], "done");
    const json row = *jobs.get(kAnna, id);
    EXPECT_EQ(row["result"]["tasks"].size(), 0u);
    EXPECT_TRUE(row["result"]["jobs"]["tasks"].is_null());
    EXPECT_TRUE(row["result"]["errors"].contains("tasks"));
    EXPECT_EQ(row["result"]["money"].size(), 1u);

    provider.replies.push_back({200, completion(json{{"tasks", bad["tasks"]}})});
    const std::string only_bad = job(false);
    EXPECT_EQ(run(only_bad)["status"], "failed") << "nothing usable is an invalid answer";
    EXPECT_EQ((*jobs.get(kAnna, only_bad))["error"].get<std::string>().rfind("invalid_answer", 0), 0u);
}

TEST(UnifiedParseHandler, IsRegisteredAmongBuiltins) {
    Core::register_job_handlers();
    EXPECT_TRUE(Jobs::Dispatcher::get().has_handler(Jobs::UnifiedParse::kJobType));
}
