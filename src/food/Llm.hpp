/**
 * @file Llm.hpp
 * @brief Settings of the LLM the food_parse job talks to.
 *
 * The provider is whatever speaks the OpenAI-compatible chat completions API:
 * the base URL, the key, the model and the prompt are configuration of the
 * worker, nothing about a provider is in the code.
 */

#pragma once

#include <optional>
#include <string>

#include "utils/Config.hpp"

namespace Food::Llm {

struct Settings {
    std::string base_url;  // without a trailing slash
    std::string api_key;
    std::string model;
    std::string prompt;
    long timeout_seconds = 60;
    int max_tokens = 1500;
};

/// The settings, or nullopt when any of base URL, key, model or prompt is
/// empty or the base URL is not http(s): the parse is then "not configured".
inline std::optional<Settings> settings() {
    if (!Config::is_initialized()) {
        return std::nullopt;
    }
    const auto& cfg = Config::get();
    Settings s;
    s.base_url = cfg.get<std::string>("food.llm.base_url", "FOOD_LLM_BASE_URL", "");
    s.api_key = cfg.get<std::string>("food.llm.api_key", "FOOD_LLM_API_KEY", "");
    s.model = cfg.get<std::string>("food.llm.model", "FOOD_LLM_MODEL", "");
    s.prompt = cfg.get<std::string>("food.llm.prompt", "FOOD_LLM_PROMPT", "");
    s.timeout_seconds = cfg.get<long>("food.llm.timeout_seconds", "FOOD_LLM_TIMEOUT_SECONDS", 60);
    s.max_tokens = cfg.get<int>("food.llm.max_tokens", "FOOD_LLM_MAX_TOKENS", 1500);
    while (!s.base_url.empty() && s.base_url.back() == '/') {
        s.base_url.pop_back();
    }
    const bool http = s.base_url.rfind("https://", 0) == 0 || s.base_url.rfind("http://", 0) == 0;
    if (!http || s.api_key.empty() || s.model.empty() || s.prompt.empty()) {
        return std::nullopt;
    }
    if (s.timeout_seconds < 5) {
        s.timeout_seconds = 5;
    }
    if (s.max_tokens < 100) {
        s.max_tokens = 100;
    }
    return s;
}

}  // namespace Food::Llm
