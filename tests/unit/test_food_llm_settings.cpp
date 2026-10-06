/**
 * @file test_food_llm_settings.cpp
 * @brief The LLM settings of the food module: complete or nothing.
 */

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "food/Llm.hpp"
#include "test_helpers.hpp"
#include "utils/Config.hpp"

namespace {

constexpr const char* kConfigFile = "food_llm_settings_config.json";

nlohmann::json complete() {
    return nlohmann::json{{"food",
                           {{"llm",
                             {{"base_url", "https://api.openai.com/v1/"},
                              {"api_key", "sk-test"},
                              {"model", "gpt-6-luna"},
                              {"prompt", "Answer with JSON."}}}}}};
}

void load(const nlohmann::json& cfg) {
    if (Config::is_initialized()) {
        Config::shutdown();
    }
    std::ofstream(kConfigFile) << cfg.dump();
    Config::initialize(kConfigFile);
}

}  // namespace

class FoodLlmSettingsTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (const char* v : {"FOOD_LLM_BASE_URL",
                              "FOOD_LLM_API_KEY",
                              "FOOD_LLM_MODEL",
                              "FOOD_LLM_PROMPT",
                              "FOOD_LLM_TIMEOUT_SECONDS",
                              "FOOD_LLM_MAX_TOKENS"}) {
            unsetenv(v);
        }
        if (Config::is_initialized()) {
            Config::shutdown();
        }
    }

    void TearDown() override {
        if (Config::is_initialized()) {
            Config::shutdown();
        }
        std::filesystem::remove(kConfigFile);
        TestHelpers::reset_all_globals();
    }
};

TEST_F(FoodLlmSettingsTest, NulloptWithoutConfig) {
    EXPECT_FALSE(Food::Llm::settings().has_value());
}

TEST_F(FoodLlmSettingsTest, CompleteSettingsAreReadWithDefaults) {
    load(complete());
    const auto s = Food::Llm::settings();
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->base_url, "https://api.openai.com/v1") << "the trailing slash goes";
    EXPECT_EQ(s->api_key, "sk-test");
    EXPECT_EQ(s->model, "gpt-6-luna");
    EXPECT_EQ(s->prompt, "Answer with JSON.");
    EXPECT_EQ(s->timeout_seconds, 60);
    EXPECT_EQ(s->max_tokens, 1500);
}

TEST_F(FoodLlmSettingsTest, AnyOfTheFourMissingMeansNotConfigured) {
    for (const char* key : {"base_url", "api_key", "model", "prompt"}) {
        auto cfg = complete();
        cfg["food"]["llm"][key] = "";
        load(cfg);
        EXPECT_FALSE(Food::Llm::settings().has_value()) << key;
    }
}

TEST_F(FoodLlmSettingsTest, BaseUrlMustBeHttp) {
    auto cfg = complete();
    cfg["food"]["llm"]["base_url"] = "api.openai.com/v1";
    load(cfg);
    EXPECT_FALSE(Food::Llm::settings().has_value());
    cfg["food"]["llm"]["base_url"] = "http://ollama.local:11434/v1";
    load(cfg);
    EXPECT_TRUE(Food::Llm::settings().has_value());
}

TEST_F(FoodLlmSettingsTest, LimitsHaveFloors) {
    auto cfg = complete();
    cfg["food"]["llm"]["timeout_seconds"] = 1;
    cfg["food"]["llm"]["max_tokens"] = 10;
    load(cfg);
    const auto s = Food::Llm::settings();
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->timeout_seconds, 5);
    EXPECT_EQ(s->max_tokens, 100);
}
