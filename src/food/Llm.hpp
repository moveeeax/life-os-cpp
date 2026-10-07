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

#include "llm/Chat.hpp"

namespace Food::Llm {

using Settings = Chat::Settings;

/// The settings, or nullopt when any of base URL, key, model or prompt is
/// empty or the base URL is not http(s): the parse is then "not configured".
inline std::optional<Settings> settings() {
    return Chat::read_settings("food.llm", "FOOD_LLM", "prompt", "FOOD_LLM_PROMPT", 1500);
}

}  // namespace Food::Llm
