/**
 * @file test_money_llm_settings.cpp
 * @brief The LLM settings of the money module: complete or nothing, apart
 *        from the food module's, the vision model falling back to the main one.
 */

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "money/Llm.hpp"
#include "test_helpers.hpp"
#include "utils/Config.hpp"

namespace {

constexpr const char* kConfigFile = "money_llm_settings_config.json";

nlohmann::json complete() {
    return nlohmann::json{{"money",
                           {{"llm",
                             {{"base_url", "https://api.openai.com/v1/"},
                              {"api_key", "sk-test"},
                              {"model", "gpt-6-luna"},
                              {"prompt_parse", "Answer with JSON."}}}}}};
}

void load(const nlohmann::json& cfg) {
    if (Config::is_initialized()) {
        Config::shutdown();
    }
    std::ofstream(kConfigFile) << cfg.dump();
    Config::initialize(kConfigFile);
}

class MoneyLlmSettingsTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (const char* v : {"MONEY_LLM_BASE_URL",
                              "MONEY_LLM_API_KEY",
                              "MONEY_LLM_MODEL",
                              "MONEY_LLM_MODEL_VISION",
                              "MONEY_LLM_PROMPT_PARSE",
                              "MONEY_LLM_TIMEOUT_SECONDS",
                              "MONEY_LLM_MAX_TOKENS",
                              "FOOD_LLM_BASE_URL",
                              "FOOD_LLM_API_KEY",
                              "FOOD_LLM_MODEL",
                              "FOOD_LLM_PROMPT"}) {
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

}  // namespace

TEST_F(MoneyLlmSettingsTest, CompleteSettingsAreReadWithDefaults) {
    load(complete());
    const auto s = Money::Llm::parse_settings();
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->base_url, "https://api.openai.com/v1");
    EXPECT_EQ(s->prompt, "Answer with JSON.");
    EXPECT_EQ(s->timeout_seconds, 60);
    EXPECT_EQ(s->max_tokens, 2500);
    EXPECT_EQ(Money::Llm::vision_model(*s), "gpt-6-luna") << "no vision model: the main one";
}

TEST_F(MoneyLlmSettingsTest, AnyOfTheFourMissingMeansNotConfigured) {
    for (const char* key : {"base_url", "api_key", "model", "prompt_parse"}) {
        auto cfg = complete();
        cfg["money"]["llm"][key] = "";
        load(cfg);
        EXPECT_FALSE(Money::Llm::parse_settings().has_value()) << key;
    }
}

TEST_F(MoneyLlmSettingsTest, TheFoodSettingsDoNotConfigureMoney) {
    load(nlohmann::json{{"food",
                         {{"llm",
                           {{"base_url", "https://api.openai.com/v1"},
                            {"api_key", "sk-test"},
                            {"model", "gpt-6-luna"},
                            {"prompt", "Food."}}}}}});
    EXPECT_FALSE(Money::Llm::parse_settings().has_value());
}

TEST_F(MoneyLlmSettingsTest, VisionModelWhenSet) {
    auto cfg = complete();
    cfg["money"]["llm"]["model_vision"] = "gpt-6-astra";
    load(cfg);
    EXPECT_EQ(Money::Llm::vision_model(*Money::Llm::parse_settings()), "gpt-6-astra");
}
