/**
 * @file test_food_parse_job.cpp
 * @brief The food_parse worker job over a scripted provider: the request it
 *        sends, the answers it accepts, and how it closes the job row.
 */

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "food/Http.hpp"
#include "jobs/BuiltinHandlers.hpp"
#include "jobs/Dispatcher.hpp"
#include "jobs/FoodParseHandler.hpp"
#include "repositories/food/ItemRepository.hpp"
#include "repositories/food/ParseJobRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa7";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb7";

class ScriptedProvider : public Food::Http::Transport {
public:
    struct Call {
        std::string url;
        json body;
        std::vector<std::pair<std::string, std::string>> headers;
        long timeout = 0;
    };
    std::vector<Food::Http::Response> replies;
    std::vector<Call> calls;
    bool fail = false;

    Food::Http::Response get(const std::string&, const std::vector<std::pair<std::string, std::string>>&) override {
        throw std::runtime_error("ScriptedProvider: GET is not expected");
    }

    Food::Http::Response post_json(const std::string& url,
                                   const std::string& body,
                                   const std::vector<std::pair<std::string, std::string>>& headers,
                                   long timeout) override {
        calls.push_back({url, json::parse(body, nullptr, false), headers, timeout});
        if (fail) {
            throw Food::Http::TransportError("timed out");
        }
        if (replies.empty()) {
            throw std::runtime_error("ScriptedProvider: unexpected request");
        }
        auto r = replies.front();
        replies.erase(replies.begin());
        return r;
    }
};

/// A chat completion whose message content is @p content.
std::string completion(const std::string& content) {
    return json{{"model", "gpt-6-luna-2026"},
                {"choices", json::array({json{{"message", json{{"role", "assistant"}, {"content", content}}}}})},
                {"usage", json{{"prompt_tokens", 321}, {"completion_tokens", 45}}}}
        .dump();
}

const char* const kGoodLines =
    R"({"lines":[{"name":"Tonkatsu curry, small","grams":300,"kcal":620,"protein_g":22,"fat_g":24,"carbs_g":78,"estimated":true}]})";

class FoodParseJobTest : public TestHelpers::CoreBackedTest {
protected:
    ScriptedProvider provider;
    Repositories::ParseJobRepository jobs;

    std::string config_file_name() const override { return "food_parse_job_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override {
        cfg["food"]["enabled"] = true;
        cfg["food"]["llm"]["base_url"] = "https://llm.example/v1/";
        cfg["food"]["llm"]["api_key"] = "sk-test";
        cfg["food"]["llm"]["model"] = "gpt-6-luna";
        cfg["food"]["llm"]["prompt"] = "Answer with JSON.";
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Food::Http::install_for_testing(&provider);
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE food_parse_jobs, food_entries, food_items, food_goals");
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

    void TearDown() override {
        Food::Http::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    std::string queued(const std::string& owner = kAnna) {
        return jobs.create(owner, "sukiya tonkatsu curry S", "lunch", "2026-10-06")["id"].get<std::string>();
    }

    json run(const std::string& id, const std::string& owner = kAnna) {
        return Jobs::FoodParse::process_job(json{{"job_id", id}, {"owner_id", owner}});
    }
};

}  // namespace

TEST_F(FoodParseJobTest, SuccessStoresTheLinesAndTheUsage) {
    Repositories::ItemRepository::Input egg;
    egg.name = "Egg";
    egg.kcal = 155;
    const auto item = Repositories::ItemRepository().create(kAnna, egg);
    Repositories::ItemRepository().create(kBoris, egg);
    const std::string id = queued();
    provider.replies.push_back({200, completion(kGoodLines)});

    const json result = run(id);
    EXPECT_EQ(result["status"], "done");
    const json row = *jobs.get(kAnna, id);
    EXPECT_EQ(row["status"], "done");
    ASSERT_EQ(row["result"].size(), 1u);
    EXPECT_EQ(row["result"][0]["name"], "Tonkatsu curry, small");
    EXPECT_EQ(row["result"][0]["kcal"], 620);
    EXPECT_EQ(row["model"], "gpt-6-luna-2026");
    EXPECT_EQ(row["prompt_tokens"], 321);
    EXPECT_EQ(row["completion_tokens"], 45);
    EXPECT_TRUE(row["error"].is_null());
    EXPECT_FALSE(row["finished_at"].is_null());

    // The request: the right URL, the key as a bearer, the prompt and the user's own items only.
    ASSERT_EQ(provider.calls.size(), 1u);
    const auto& call = provider.calls[0];
    EXPECT_EQ(call.url, "https://llm.example/v1/chat/completions");
    EXPECT_EQ(call.timeout, 60);
    bool bearer = false;
    for (const auto& [k, v] : call.headers) {
        bearer = bearer || (k == "Authorization" && v == "Bearer sk-test");
    }
    EXPECT_TRUE(bearer);
    EXPECT_EQ(call.body["model"], "gpt-6-luna");
    EXPECT_EQ(call.body["messages"][0]["content"], "Answer with JSON.");
    EXPECT_EQ(call.body["response_format"]["type"], "json_object");
    const json user = json::parse(call.body["messages"][1]["content"].get<std::string>());
    EXPECT_EQ(user["text"], "sukiya tonkatsu curry S");
    EXPECT_EQ(user["meal"], "lunch");
    ASSERT_EQ(user["items"].size(), 1u);
    EXPECT_EQ(user["items"][0]["id"], item["id"]);
    // Nothing reached the diary.
    const long entries = Database::get().execute_read(
        [](auto& txn) { return txn.exec("SELECT COUNT(*) FROM food_entries")[0][0].template as<long>(); });
    EXPECT_EQ(entries, 0);
}

TEST_F(FoodParseJobTest, ResponseFormatRefusalIsRetriedOnceWithoutIt) {
    const std::string id = queued();
    provider.replies.push_back({400, R"({"error":{"message":"response_format is not supported"}})"});
    provider.replies.push_back({200, completion(kGoodLines)});
    EXPECT_EQ(run(id)["status"], "done");
    ASSERT_EQ(provider.calls.size(), 2u);
    EXPECT_TRUE(provider.calls[0].body.contains("response_format"));
    EXPECT_FALSE(provider.calls[1].body.contains("response_format"));
}

TEST_F(FoodParseJobTest, OtherProviderErrorsAndRefusalsFailWithoutRetry) {
    const std::string a = queued();
    provider.replies.push_back({400, R"({"error":{"message":"model not found"}})"});
    EXPECT_EQ(run(a)["error"], "provider_error_400");
    EXPECT_EQ((*jobs.get(kAnna, a))["status"], "failed");
    EXPECT_EQ(provider.calls.size(), 1u);

    const std::string b = queued();
    provider.replies.push_back({401, R"({"error":"bad key"})"});
    EXPECT_EQ(run(b)["error"], "provider_refused");
    EXPECT_EQ((*jobs.get(kAnna, b))["error"], "provider_refused");
}

TEST_F(FoodParseJobTest, InvalidAnswerFails) {
    const std::string id = queued();
    provider.replies.push_back({200, completion("Sorry, I cannot help with that.")});
    EXPECT_EQ(run(id)["error"], "invalid_answer");
    const json row = *jobs.get(kAnna, id);
    EXPECT_EQ(row["status"], "failed");
    EXPECT_EQ(row["error"].get<std::string>().rfind("invalid_answer", 0), 0u);
    EXPECT_TRUE(row["result"].is_null());
}

TEST_F(FoodParseJobTest, ForeignItemIdFails) {
    Repositories::ItemRepository::Input egg;
    egg.name = "Egg";
    egg.kcal = 155;
    const auto boris_item = Repositories::ItemRepository().create(kBoris, egg);
    const std::string id = queued();
    provider.replies.push_back({200,
                                completion(R"({"lines":[{"name":"Egg","grams":60,"kcal":93,"item_id":")" +
                                           boris_item["id"].get<std::string>() + R"("}]})")});
    EXPECT_EQ(run(id)["error"], "invalid_answer");
}

TEST_F(FoodParseJobTest, OutagesThrowForTheQueueAndRequeueTheJob) {
    const std::string id = queued();
    provider.replies.push_back({503, "busy"});
    EXPECT_THROW(run(id), std::runtime_error);
    EXPECT_EQ((*jobs.get(kAnna, id))["status"], "queued") << "the retry can claim it again";

    provider.fail = true;
    EXPECT_THROW(run(id), std::runtime_error);
    EXPECT_EQ((*jobs.get(kAnna, id))["status"], "queued");

    provider.fail = false;
    provider.replies.push_back({200, completion(kGoodLines)});
    EXPECT_EQ(run(id)["status"], "done");
}

TEST_F(FoodParseJobTest, RedeliveryOfAFinishedJobDoesNothing) {
    const std::string id = queued();
    provider.replies.push_back({200, completion(kGoodLines)});
    ASSERT_EQ(run(id)["status"], "done");
    const json again = run(id);
    EXPECT_EQ(again["status"], "done");
    EXPECT_EQ(provider.calls.size(), 1u) << "no second request";
    EXPECT_EQ(run("cccccccc-cccc-4ccc-8ccc-ccccccccccc7")["status"], "missing");
}

namespace {

class FoodParseUnconfiguredTest : public TestHelpers::CoreBackedTest {
protected:
    std::string config_file_name() const override { return "food_parse_unconfigured_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["food"]["enabled"] = true; }
};

class FoodParseDisabledTest : public TestHelpers::CoreBackedTest {
protected:
    std::string config_file_name() const override { return "food_parse_disabled_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["food"]["enabled"] = false; }
};

}  // namespace

TEST_F(FoodParseUnconfiguredTest, MissingSettingsFailTheJob) {
    Database::get().execute_write([](auto& txn) {
        txn.exec_params(
            "INSERT INTO users (id, email, confirmed, role_id) VALUES ($1::uuid, $2, TRUE, "
            "(SELECT id FROM roles ORDER BY id LIMIT 1)) ON CONFLICT DO NOTHING",
            std::string(kAnna),
            std::string(kAnna) + "@example.test");
        return true;
    });
    Repositories::ParseJobRepository jobs;
    const std::string id = jobs.create(kAnna, "tea", "snack", "2026-10-06")["id"].get<std::string>();
    EXPECT_EQ(Jobs::FoodParse::process_job(json{{"job_id", id}, {"owner_id", kAnna}})["error"], "not_configured");
    EXPECT_EQ((*jobs.get(kAnna, id))["status"], "failed");
}

TEST_F(FoodParseDisabledTest, DisabledModuleFailsTheJob) {
    Repositories::ParseJobRepository jobs;
    const std::string id = jobs.create(kAnna, "tea", "snack", "2026-10-06")["id"].get<std::string>();
    EXPECT_EQ(Jobs::FoodParse::process_job(json{{"job_id", id}, {"owner_id", kAnna}})["error"], "food_disabled");
}

TEST(FoodParseHandler, IsRegisteredAmongBuiltins) {
    Jobs::register_builtin_handlers();
    EXPECT_TRUE(Jobs::Dispatcher::get().has_handler(Jobs::FoodParse::kJobType));
    EXPECT_STREQ(Jobs::FoodParse::kJobType, "food_parse");
}
