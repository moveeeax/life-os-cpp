/**
 * @file test_money_parse_job.cpp
 * @brief The money_parse job with a scripted provider and a real database:
 *        what the request carries, foreign ids nulled, the receipt photo sent
 *        and cleared, the provider rules shared with the food parse.
 */

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "jobs/BuiltinHandlers.hpp"
#include "jobs/Dispatcher.hpp"
#include "jobs/MoneyParseHandler.hpp"
#include "net/Http.hpp"
#include "repositories/money/AccountRepository.hpp"
#include "repositories/money/CategoryRepository.hpp"
#include "repositories/money/CurrencyRepository.hpp"
#include "repositories/money/ParseJobRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa8";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb8";

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

std::string completion(const json& content) {
    return json{{"model", "gpt-6-luna-2026"},
                {"choices", json::array({json{{"message", json{{"role", "assistant"}, {"content", content.dump()}}}}})},
                {"usage", json{{"prompt_tokens", 400}, {"completion_tokens", 90}}}}
        .dump();
}

class MoneyParseJobTest : public TestHelpers::CoreBackedTest {
protected:
    ScriptedProvider provider;
    Repositories::Money::ParseJobRepository jobs;
    json kaspi, boris_account, food, boris_food;

    std::string config_file_name() const override { return "money_parse_job_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override {
        cfg["money"]["enabled"] = true;
        cfg["money"]["llm"]["base_url"] = "https://llm.example/v1/";
        cfg["money"]["llm"]["api_key"] = "sk-test";
        cfg["money"]["llm"]["model"] = "gpt-6-luna";
        cfg["money"]["llm"]["prompt_parse"] = "Answer with JSON.";
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Net::Http::install_for_testing(&provider);
        Database::get().execute_write([](auto& txn) {
            txn.exec(
                "TRUNCATE TABLE money_parse_jobs, money_merchants, money_transfers, money_transactions, "
                "money_categories, money_accounts, money_currencies CASCADE");
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
        Repositories::Money::CurrencyRepository().seed_defaults(kBoris);
        Repositories::Money::AccountRepository::Input a;
        a.name = "Kaspi Gold Visa •8880";
        a.bank = "Kaspi";
        a.currency = "KZT";
        a.last4 = "8880";
        kaspi = Repositories::Money::AccountRepository().create(kAnna, a);
        boris_account = Repositories::Money::AccountRepository().create(kBoris, a);
        Repositories::Money::CategoryRepository::Input c;
        c.name = "Food";
        food = Repositories::Money::CategoryRepository().create(kAnna, c);
        boris_food = Repositories::Money::CategoryRepository().create(kBoris, c);
    }

    void TearDown() override {
        Net::Http::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    std::string text_job(const std::string& text = "Kaspi Gold *8880: purchase 13275.61 KZT BIG C PHUKET") {
        return jobs.create_text(kAnna, text, "2026-10-07")["id"].get<std::string>();
    }

    json run(const std::string& id, int max_attempts = 3) {
        return Jobs::MoneyParse::process_job(json{{"job_id", id}, {"owner_id", kAnna}, {"max_attempts", max_attempts}});
    }

    json good_lines() const {
        return json{{"lines",
                     {{{"date", "2026-10-05"},
                       {"account_id", kaspi["id"]},
                       {"amount", 13275.61},
                       {"merchant", "BIG C PHUKET"},
                       {"name", "Groceries (store, THB)"},
                       {"category_id", food["id"]},
                       {"confidence", 0.9}}}}};
    }
};

}  // namespace

TEST_F(MoneyParseJobTest, TextSuccessCarriesTheUsersDataAndStoresTheLines) {
    provider.replies.push_back({200, completion(good_lines())});
    const std::string id = text_job();
    EXPECT_EQ(run(id)["status"], "done");

    ASSERT_EQ(provider.bodies.size(), 1u);
    const json& req = provider.bodies[0];
    EXPECT_EQ(req["model"], "gpt-6-luna");
    EXPECT_EQ(req["messages"][0]["content"], "Answer with JSON.");
    const json user = json::parse(req["messages"][1]["content"].get<std::string>());
    EXPECT_EQ(user["hint_date"], "2026-10-07");
    ASSERT_EQ(user["accounts"].size(), 1u) << "only the owner's accounts";
    EXPECT_EQ(user["accounts"][0]["last4"], "8880");
    EXPECT_EQ(user["categories"].size(), 1u);
    EXPECT_NE(user["text"].get<std::string>().find("8880"), std::string::npos);

    const json row = *jobs.get(kAnna, id);
    EXPECT_EQ(row["status"], "done");
    EXPECT_EQ(row["model"], "gpt-6-luna-2026");
    EXPECT_EQ(row["prompt_tokens"], 400);
    ASSERT_EQ(row["result"].size(), 1u);
    EXPECT_EQ(row["result"][0]["account_id"], kaspi["id"]);
    EXPECT_FALSE(jobs.get(kBoris, id).has_value());
}

TEST_F(MoneyParseJobTest, ForeignIdsAreNulledNotFatal) {
    json lines = good_lines();
    lines["lines"][0]["account_id"] = boris_account["id"];
    lines["lines"][0]["category_id"] = boris_food["id"];
    lines["lines"].push_back({{"amount", 5}, {"name", "Coffee"}});
    provider.replies.push_back({200, completion(lines)});
    const std::string id = text_job();
    EXPECT_EQ(run(id)["status"], "done");
    const json result = (*jobs.get(kAnna, id))["result"];
    ASSERT_EQ(result.size(), 2u) << "the other line survives";
    EXPECT_TRUE(result[0]["account_id"].is_null());
    EXPECT_TRUE(result[0]["category_id"].is_null());
    EXPECT_NE(result[0]["note"].get<std::string>().find("account not matched"), std::string::npos);
}

TEST_F(MoneyParseJobTest, AReceiptSendsThePhotoAndClearsIt) {
    provider.replies.push_back({200, completion(good_lines())});
    const std::string id = jobs.create_receipt(kAnna, "aGVsbG8=", "image/png", "2026-10-07")["id"].get<std::string>();
    EXPECT_TRUE(jobs.has_image(id));
    EXPECT_EQ(run(id)["status"], "done");
    const json content = provider.bodies[0]["messages"][1]["content"];
    ASSERT_TRUE(content.is_array());
    EXPECT_EQ(content[1]["type"], "image_url");
    EXPECT_EQ(content[1]["image_url"]["url"], "data:image/png;base64,aGVsbG8=");
    EXPECT_FALSE(jobs.has_image(id)) << "the photo goes when the job ends";

    // A failed receipt clears it too.
    provider.replies.push_back({200, completion(json{{"lines", json::array()}})});
    const std::string bad = jobs.create_receipt(kAnna, "aGVsbG8=", "image/png", "2026-10-07")["id"].get<std::string>();
    EXPECT_EQ(run(bad)["error"], "invalid_answer");
    EXPECT_FALSE(jobs.has_image(bad));
}

TEST_F(MoneyParseJobTest, ProviderRulesAreTheSharedOnes) {
    provider.replies.push_back(
        {400, R"({"error":{"message":"Unsupported parameter: 'max_tokens' is not supported with this model."}})"});
    provider.replies.push_back({200, completion(good_lines())});
    const std::string id = text_job();
    EXPECT_EQ(run(id)["status"], "done");
    ASSERT_EQ(provider.bodies.size(), 2u);
    EXPECT_TRUE(provider.bodies[1].contains("max_completion_tokens"));

    provider.replies.push_back({401, R"({"error":"bad key"})"});
    EXPECT_EQ(run(text_job())["error"], "provider_refused");
}

TEST_F(MoneyParseJobTest, OutagesRequeueUntilTheLastAttempt) {
    provider.fail = true;
    const std::string id = text_job();
    EXPECT_THROW(run(id, 2), std::runtime_error);
    EXPECT_EQ((*jobs.get(kAnna, id))["status"], "queued");
    EXPECT_EQ(run(id, 2)["error"], "provider_unavailable");
    EXPECT_EQ((*jobs.get(kAnna, id))["status"], "failed");
}

TEST_F(MoneyParseJobTest, AStaleRunningRowIsClaimedAgainAndAFinishedOneIsLeftAlone) {
    const std::string id = text_job();
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params(
            "UPDATE money_parse_jobs SET status = 'running', started_at = now() - interval '11 minutes' "
            "WHERE id = $1::uuid",
            id);
        return true;
    });
    provider.replies.push_back({200, completion(good_lines())});
    EXPECT_EQ(run(id)["status"], "done");
    EXPECT_EQ(run(id)["status"], "done") << "a redelivery does nothing";
    EXPECT_EQ(provider.bodies.size(), 1u);
}

TEST(MoneyParseHandler, IsRegisteredAmongBuiltins) {
    Jobs::register_builtin_handlers();
    EXPECT_TRUE(Jobs::Dispatcher::get().has_handler(Jobs::MoneyParse::kJobType));
}

namespace {

class MoneyParseUnconfiguredTest : public MoneyParseJobTest {
protected:
    std::string config_file_name() const override { return "money_parse_unconfigured_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        MoneyParseJobTest::config_overrides(cfg);
        cfg["money"]["llm"]["prompt_parse"] = "";
    }
};

}  // namespace

TEST_F(MoneyParseUnconfiguredTest, AJobWithoutSettingsFailsWithoutCallingAnyone) {
    const std::string id = text_job();
    EXPECT_EQ(run(id)["error"], "not_configured");
    EXPECT_EQ((*jobs.get(kAnna, id))["status"], "failed");
    EXPECT_TRUE(provider.bodies.empty());
}
