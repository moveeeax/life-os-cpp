/**
 * @file test_parse_api.cpp
 * @brief The unified parse route (POST /api/v1/parse) and the parses that
 *        apply their own rows: one phrase queued without any later client
 *        call ends as rows, a retry with the same Idempotency-Key finds the
 *        same job, the open-job cap, the shapes against the spec.
 */

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/MoneyController.hpp"
#include "api/ParseController.hpp"
#include "database/Database.hpp"
#include "jobs/Jobs.hpp"
#include "money/jobs/ParseJob.hpp"
#include "net/Http.hpp"
#include "parse/jobs/ParseJob.hpp"
#include "repositories/money/AccountRepository.hpp"
#include "repositories/money/CategoryRepository.hpp"
#include "repositories/money/CurrencyRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaab3";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbc3";

using TestHelpers::body_of;
using TestHelpers::user;

class ScriptedProvider : public Net::Http::Transport {
public:
    std::vector<std::string> replies;

    Net::Http::Response get(const std::string&, const std::vector<std::pair<std::string, std::string>>&) override {
        throw std::runtime_error("ScriptedProvider: GET is not expected");
    }

    Net::Http::Response post_json(const std::string&,
                                  const std::string&,
                                  const std::vector<std::pair<std::string, std::string>>&,
                                  long) override {
        if (replies.empty()) {
            throw std::runtime_error("ScriptedProvider: unexpected request");
        }
        const std::string content = replies.front();
        replies.erase(replies.begin());
        return {200,
                json{{"model", "gpt-6-luna"},
                     {"choices", json::array({json{{"message", json{{"role", "assistant"}, {"content", content}}}}})}}
                    .dump()};
    }
};

class ParseApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::ParseController controller;
    Api::MoneyController money;
    ScriptedProvider provider;
    json kaspi, cafe;

    std::string config_file_name() const override { return "parse_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["jobs"]["enabled"] = true;
        cfg["jobs"]["result_ttl"] = 3600;
        cfg["food"]["enabled"] = true;
        cfg["food"]["llm"]["base_url"] = "https://llm.example/v1";
        cfg["food"]["llm"]["api_key"] = "sk-test";
        cfg["food"]["llm"]["model"] = "gpt-6-luna";
        cfg["food"]["llm"]["prompt"] = "Food JSON.";
        cfg["money"]["enabled"] = true;
        cfg["money"]["llm"]["base_url"] = "https://llm.example/v1";
        cfg["money"]["llm"]["api_key"] = "sk-test";
        cfg["money"]["llm"]["model"] = "gpt-6-luna";
        cfg["money"]["llm"]["prompt_parse"] = "Money JSON.";
        cfg["tasks"]["enabled"] = true;
        cfg["tasks"]["llm"]["prompt_parse"] = "Tasks JSON.";
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
        a.name = "Kaspi Gold";
        a.currency = "KZT";
        kaspi = Repositories::Money::AccountRepository().create(kAnna, a);
        Repositories::Money::CategoryRepository::Input c;
        c.name = "Cafe";
        cafe = Repositories::Money::CategoryRepository().create(kAnna, c);
    }

    void TearDown() override {
        Net::Http::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    template <typename Ctrl>
    HttpResponsePtr post(Ctrl& ctrl,
                         void (Ctrl::*h)(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&),
                         const char* who,
                         const json& body,
                         const std::string& key = "") {
        auto req = TestHelpers::authed_json(user(who), body);
        if (!key.empty()) {
            req->addHeader("Idempotency-Key", key);
        }
        HttpResponsePtr resp;
        (ctrl.*h)(req, [&](const HttpResponsePtr& r) { resp = r; });
        return resp;
    }

    HttpResponsePtr parse(const char* who, const json& body, const std::string& key = "") {
        auto resp = post(controller, &Api::ParseController::start, who, body, key);
        TestHelpers::expect_2xx_matches_schema(resp, "POST", "/api/v1/parse");
        return resp;
    }

    HttpResponsePtr status(const char* who, const std::string& id) {
        HttpResponsePtr resp;
        controller.status(
            TestHelpers::authed(user(who)), [&](const HttpResponsePtr& r) { resp = r; }, id);
        TestHelpers::expect_2xx_matches_schema(resp, "GET", "/api/v1/parse/{id}");
        return resp;
    }

    /// The one queued job of @p type, run by its handler as the worker would.
    json work(const char* type) {
        auto job = Jobs::get().pick({type}, 1);
        EXPECT_TRUE(job.has_value()) << type;
        if (!job) {
            return json();
        }
        Jobs::get().complete(job->id, json::object());
        if (std::string(type) == Jobs::MoneyParse::kJobType) {
            return Jobs::MoneyParse::process_job(job->payload);
        }
        return Jobs::UnifiedParse::process_job(job->payload);
    }

    bool queue_empty(const char* type) { return !Jobs::get().pick({type}, 1).has_value(); }

    long count(const std::string& sql) {
        return Database::get().execute_read(
            [&](auto& txn) { return txn.exec_params(sql, std::string(kAnna))[0][0].template as<long>(); });
    }
};

}  // namespace

// The acceptance of the offline queue: one POST, no other call, a posted spend; the retry creates nothing.
TEST_F(ParseApiTest, AMoneyParseWithApplyPostsTheSpendOnceForOneKey) {
    provider.replies.push_back(
        json{{"lines", {{{"amount", 1200}, {"name", "Кофе"}, {"category_id", cafe["id"]}}}}}.dump());
    const json body{{"text", "кофе 1200"}, {"apply", true}};
    const auto first = post(money, &Api::MoneyController::parseText, kAnna, body, "queue-1");
    ASSERT_EQ(first->statusCode(), k202Accepted) << first->body();
    TestHelpers::expect_matches_schema(first, "POST", "/api/v1/money/parse");
    EXPECT_EQ(work(Jobs::MoneyParse::kJobType)["status"], "done");
    EXPECT_EQ(count("SELECT count(*) FROM money_transactions WHERE owner_id = $1::uuid AND status = 'posted'"), 1);

    const auto again = post(money, &Api::MoneyController::parseText, kAnna, body, "queue-1");
    ASSERT_EQ(again->statusCode(), k202Accepted) << again->body();
    TestHelpers::expect_matches_schema(again, "POST", "/api/v1/money/parse");
    EXPECT_EQ(body_of(again)["data"]["id"], body_of(first)["data"]["id"]) << "the same job";
    EXPECT_EQ(body_of(again)["data"]["status"], "done");
    EXPECT_TRUE(queue_empty(Jobs::MoneyParse::kJobType)) << "no second queue job";
    EXPECT_EQ(count("SELECT count(*) FROM money_transactions WHERE owner_id = $1::uuid"), 1) << "no second spend";

    HttpResponsePtr job;
    money.parseStatus(
        TestHelpers::authed(user(kAnna)), [&](const HttpResponsePtr& r) { job = r; }, body_of(first)["data"]["id"]);
    ASSERT_EQ(job->statusCode(), k200OK);
    TestHelpers::expect_matches_schema(job, "GET", "/api/v1/money/parse/{id}");
    EXPECT_EQ(body_of(job)["data"]["applied"]["money"][0]["status"], "posted");
}

TEST_F(ParseApiTest, OnePhraseIsOneJobWithEveryKind) {
    provider.replies.push_back(json{
        {"food", {{"lines", {{{"name", "Творог"}, {"grams", 200}, {"kcal", 242}}}}}},
        {"money", {{"lines", {{{"amount", 1200}, {"name", "Кофе"}, {"category_id", cafe["id"]}}}}}},
        {"tasks", {{"lines", {{{"title", "Позвонить маме"}, {"area", "relationships"}, {"due", "2026-10-08"}}}}}},
        {"water", nullptr}}.dump());
    const auto queued = parse(kAnna,
                              json{{"text", "кофе 1200 и творог 200 г, завтра позвонить маме"},
                                   {"meal", "breakfast"},
                                   {"date", "2026-10-07"},
                                   {"tz", "Asia/Almaty"},
                                   {"apply", true}},
                              "queue-2");
    ASSERT_EQ(queued->statusCode(), k202Accepted) << queued->body();
    const std::string id = body_of(queued)["data"]["id"];
    EXPECT_EQ(work(Jobs::UnifiedParse::kJobType)["status"], "done");

    const auto read = status(kAnna, id);
    ASSERT_EQ(read->statusCode(), k200OK) << read->body();
    const json row = body_of(read)["data"];
    EXPECT_EQ(row["result"]["food"].size(), 1u);
    EXPECT_EQ(row["result"]["money"].size(), 1u);
    EXPECT_EQ(row["result"]["tasks"].size(), 1u);
    EXPECT_EQ(row["applied"]["money"][0]["status"], "posted");
    EXPECT_EQ(count("SELECT count(*) FROM money_transactions WHERE owner_id = $1::uuid"), 1);
    EXPECT_EQ(count("SELECT count(*) FROM food_entries WHERE owner_id = $1::uuid AND meal = 'breakfast'"), 1);
    EXPECT_EQ(count("SELECT count(*) FROM task_items WHERE owner_id = $1::uuid"), 1);

    EXPECT_EQ(body_of(parse(
                  kAnna, json{{"text", "кофе 1200 и творог 200 г, завтра позвонить маме"}}, "queue-2"))["data"]["id"],
              id)
        << "a retry with the key finds the job";
    EXPECT_TRUE(queue_empty(Jobs::UnifiedParse::kJobType));
    EXPECT_EQ(status(kBoris, id)->statusCode(), k404NotFound);
}

TEST_F(ParseApiTest, ThreeOpenPhrasesAndARetryStillAnswers) {
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(parse(kAnna, json{{"text", "x"}}, "k" + std::to_string(i))->statusCode(), k202Accepted);
    }
    EXPECT_EQ(parse(kAnna, json{{"text", "x"}})->statusCode(), k429TooManyRequests);
    EXPECT_EQ(parse(kAnna, json{{"text", "x"}}, "k1")->statusCode(), k202Accepted) << "a retry is not a new parse";
    const auto money_parse = post(money, &Api::MoneyController::parseText, kAnna, json{{"text", "x"}});
    EXPECT_EQ(money_parse->statusCode(), k202Accepted) << "the module parses keep their own places";
    EXPECT_EQ(parse(kBoris, json{{"text", "x"}})->statusCode(), k202Accepted);
    for (int i = 0; i < 4; ++i) {  // three of Anna's and one of Boris's
        auto job = Jobs::get().pick({Jobs::UnifiedParse::kJobType}, 1);
        ASSERT_TRUE(job.has_value());
        Jobs::get().complete(job->id, json::object());
    }
    auto job = Jobs::get().pick({Jobs::MoneyParse::kJobType}, 1);
    ASSERT_TRUE(job.has_value());
    Jobs::get().complete(job->id, json::object());
}

TEST_F(ParseApiTest, TheBodyIsChecked) {
    EXPECT_EQ(parse(kAnna, json{{"text", ""}})->statusCode(), k400BadRequest);
    EXPECT_EQ(parse(kAnna, json{{"text", "x"}, {"meal", "brunch"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(parse(kAnna, json{{"text", "x"}, {"date", "2026-02-30"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(parse(kAnna, json{{"text", "x"}, {"tz", "Mars/Olympus"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(parse(kAnna, json{{"text", "x"}, {"apply", "yes"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(parse(kAnna, json{{"text", "x"}, {"account_id", "nope"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(parse(kAnna, json{{"text", "x"}}, std::string(201, 'k'))->statusCode(), k400BadRequest);
    EXPECT_TRUE(queue_empty(Jobs::UnifiedParse::kJobType));
}
