/**
 * @file test_money_advisor_job.cpp
 * @brief The money_advisor job with a scripted provider and a real database:
 *        the facts it sends, a period without rows, the shared provider
 *        rules, and the weekly enqueue that a second tick cannot repeat.
 */

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "jobs/BuiltinHandlers.hpp"
#include "jobs/Dispatcher.hpp"
#include "jobs/Jobs.hpp"
#include "jobs/MoneyAdvisorHandler.hpp"
#include "net/Http.hpp"
#include "repositories/money/AccountRepository.hpp"
#include "repositories/money/AdvisorReportRepository.hpp"
#include "repositories/money/CategoryRepository.hpp"
#include "repositories/money/CurrencyRepository.hpp"
#include "repositories/money/SettingsRepository.hpp"
#include "repositories/money/TransactionRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa9";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb9";

class ScriptedProvider : public Net::Http::Transport {
public:
    std::vector<Net::Http::Response> replies;
    std::vector<json> bodies;
    bool fail = false;

    Net::Http::Response get(const std::string&, const std::vector<std::pair<std::string, std::string>>&) override {
        throw std::runtime_error("ScriptedProvider: GET is not expected");
    }
    Net::Http::Response post_json(const std::string&,
                                  const std::string& body,
                                  const std::vector<std::pair<std::string, std::string>>&,
                                  long) override {
        bodies.push_back(json::parse(body, nullptr, false));
        if (fail) {
            throw Net::Http::TransportError("timed out");
        }
        if (replies.empty()) {
            throw std::runtime_error("ScriptedProvider: unexpected request");
        }
        auto r = replies.front();
        replies.erase(replies.begin());
        return r;
    }
};

std::string completion(const std::string& content) {
    return json{{"model", "gpt-6-luna-2026"},
                {"choices", json::array({json{{"message", json{{"role", "assistant"}, {"content", content}}}}})},
                {"usage", json{{"prompt_tokens", 700}, {"completion_tokens", 200}}}}
        .dump();
}

class MoneyAdvisorJobTest : public TestHelpers::CoreBackedTest {
protected:
    ScriptedProvider provider;
    Repositories::Money::AdvisorReportRepository reports;
    json kaspi, rent;

    std::string config_file_name() const override { return "money_advisor_job_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override {
        cfg["money"]["enabled"] = true;
        cfg["money"]["llm"]["base_url"] = "https://llm.example/v1";
        cfg["money"]["llm"]["api_key"] = "sk-test";
        cfg["money"]["llm"]["model"] = "gpt-6-luna";
        cfg["money"]["llm"]["prompt_advisor"] = "Write a review.";
        cfg["jobs"]["enabled"] = true;
        cfg["jobs"]["result_ttl"] = 3600;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Net::Http::install_for_testing(&provider);
        Database::get().execute_write([](auto& txn) {
            txn.exec(
                "TRUNCATE TABLE money_advisor_reports, money_settings, money_merchants, money_transfers, "
                "money_transactions, money_categories, money_accounts, money_currencies CASCADE");
            for (const char* id : {kAnna, kBoris}) {
                txn.exec_params(
                    "INSERT INTO users (id, email, confirmed, role_id) "
                    "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) ON CONFLICT DO NOTHING",
                    std::string(id),
                    std::string(id) + "@example.test");
            }
            return true;
        });
        Repositories::Money::CurrencyRepository().seed_defaults(kAnna);
        Repositories::Money::AccountRepository::Input a;
        a.name = "Kaspi";
        a.currency = "KZT";
        a.opening_balance = 1000;
        kaspi = Repositories::Money::AccountRepository().create(kAnna, a);
        Repositories::Money::CategoryRepository::Input c;
        c.name = "Rent";
        c.flexibility = "fixed";
        rent = Repositories::Money::CategoryRepository().create(kAnna, c);
    }

    void TearDown() override {
        Net::Http::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    void expense(const std::string& date, double amount) {
        Repositories::Money::TransactionRepository::Input t;
        t.date = date;
        t.account_id = kaspi["id"];
        t.amount = amount;
        t.category_id = rent["id"];
        t.merchant = "Landlord";
        t.name = "Rent";
        Repositories::Money::TransactionRepository().create(kAnna, t);
    }

    json run(const std::string& id, const std::string& today = "2026-10-12") {
        return Jobs::MoneyAdvisor::process_job(
            json{{"report_id", id}, {"owner_id", kAnna}, {"max_attempts", 2}, {"today", today}});
    }
};

}  // namespace

TEST_F(MoneyAdvisorJobTest, TheFactsAreTheReportWithNamesBalancesAndTheNote) {
    expense("2026-10-06", 250000);
    Repositories::Money::SettingsRepository::Input s;
    s.advisor_note = "Копим на квартиру";
    Repositories::Money::SettingsRepository().put(kAnna, s);
    const auto [row, queued] = reports.create_or_get(kAnna, "week", "2026-10-05", "2026-10-11");
    ASSERT_TRUE(queued);
    provider.replies.push_back({200, completion("## What happened\nRent took most of the week.")});
    EXPECT_EQ(run(row["id"])["status"], "done");

    ASSERT_EQ(provider.bodies.size(), 1u);
    EXPECT_EQ(provider.bodies[0]["messages"][0]["content"], "Write a review.");
    const json facts = json::parse(provider.bodies[0]["messages"][1]["content"].get<std::string>());
    EXPECT_EQ(facts["period"]["from"], "2026-10-05");
    ASSERT_EQ(facts["blocks"].size(), 1u);
    EXPECT_EQ(facts["blocks"][0]["currency"], "KZT");
    EXPECT_EQ(facts["blocks"][0]["categories"][0]["category"], "Rent") << "names, not ids";
    EXPECT_FALSE(facts["blocks"][0]["categories"][0].contains("category_id"));
    EXPECT_EQ(facts["balances"][0]["currency"], "KZT");
    EXPECT_FALSE(facts["partial_period"]) << "the week ended before today";
    EXPECT_EQ(facts["note"], "Копим на квартиру");

    const json stored = *reports.get(kAnna, row["id"]);
    EXPECT_EQ(stored["status"], "done");
    EXPECT_EQ(stored["content"], "## What happened\nRent took most of the week.");
    EXPECT_EQ(stored["facts"]["blocks"][0]["currency"], "KZT") << "the facts are kept with the text";
    EXPECT_EQ(stored["prompt_tokens"], 700);
    EXPECT_FALSE(reports.get(kBoris, row["id"]).has_value());
}

TEST_F(MoneyAdvisorJobTest, ARunningPeriodIsMarkedPartial) {
    expense("2026-10-06", 100);
    const auto [row, queued] = reports.create_or_get(kAnna, "month", "2026-10-01", "2026-10-31");
    provider.replies.push_back({200, completion("ok")});
    run(row["id"], "2026-10-12");
    const json facts = json::parse(provider.bodies[0]["messages"][1]["content"].get<std::string>());
    EXPECT_TRUE(facts["partial_period"]);
}

TEST_F(MoneyAdvisorJobTest, APeriodWithoutRowsGetsALineAndNoCall) {
    const auto [row, queued] = reports.create_or_get(kAnna, "week", "2026-10-05", "2026-10-11");
    EXPECT_EQ(run(row["id"])["status"], "done");
    EXPECT_TRUE(provider.bodies.empty());
    EXPECT_EQ((*reports.get(kAnna, row["id"]))["content"], Jobs::MoneyAdvisor::kNothing);
}

TEST_F(MoneyAdvisorJobTest, ProviderRulesAndRetries) {
    expense("2026-10-06", 100);
    const auto [row, queued] = reports.create_or_get(kAnna, "week", "2026-10-05", "2026-10-11");
    provider.replies.push_back({400, R"({"error":{"message":"'temperature' does not support 0.3 with this model."}})"});
    provider.replies.push_back({200, completion("ok")});
    EXPECT_EQ(run(row["id"])["status"], "done");
    EXPECT_FALSE(provider.bodies[1].contains("temperature"));

    expense("2026-09-10", 100);  // a period with rows: the provider is asked
    const auto [other, q2] = reports.create_or_get(kAnna, "month", "2026-09-01", "2026-09-30");
    provider.fail = true;
    EXPECT_THROW(run(other["id"]), std::runtime_error);
    EXPECT_EQ((*reports.get(kAnna, other["id"]))["status"], "queued");
    EXPECT_EQ(run(other["id"])["error"], "provider_unavailable");

    // A failed review can be asked for again.
    const auto [again, requeued] = reports.create_or_get(kAnna, "month", "2026-09-01", "2026-09-30");
    EXPECT_TRUE(requeued);
    EXPECT_EQ(again["status"], "queued");
}

TEST_F(MoneyAdvisorJobTest, TheWeeklyEnqueueHappensOncePerUserAndWeek) {
    Repositories::Money::SettingsRepository::Input s;
    s.advisor_enabled = true;
    s.advisor_weekday = 1;
    Repositories::Money::SettingsRepository().put(kAnna, s);
    EXPECT_EQ(Jobs::MoneyAdvisor::enqueue_weekly(1, "2026-10-12"), 1);
    EXPECT_EQ(Jobs::MoneyAdvisor::enqueue_weekly(1, "2026-10-12"), 0) << "the second tick finds the row";
    EXPECT_EQ(Jobs::MoneyAdvisor::enqueue_weekly(2, "2026-10-13"), 0) << "not Anna's weekday";
    auto job = Jobs::get().pick({"money_advisor"}, 1);
    ASSERT_TRUE(job.has_value());
    const json row = *reports.get(kAnna, job->payload["report_id"]);
    EXPECT_EQ(row["period_start"], "2026-10-05") << "the week that just ended";
    EXPECT_EQ(row["period_end"], "2026-10-11");
    Jobs::get().complete(job->id, json::object());
}

TEST(MoneyAdvisorHandler, IsRegisteredAmongBuiltins) {
    Jobs::register_builtin_handlers();
    EXPECT_TRUE(Jobs::Dispatcher::get().has_handler(Jobs::MoneyAdvisor::kJobType));
}
