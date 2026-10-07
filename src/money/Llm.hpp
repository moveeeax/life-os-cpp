/**
 * @file Llm.hpp
 * @brief Settings of the LLM the money module talks to (MONEY_LLM_*), worker
 *        configuration like the food module's and separate from it, so each
 *        module can point at its own provider, model and key.
 */

#pragma once

#include <optional>
#include <string>

#include "llm/Chat.hpp"
#include "utils/Config.hpp"

namespace Money::Llm {

/// The parse settings, or nullopt when base URL, key, model or the parse prompt is missing.
inline std::optional<Chat::Settings> parse_settings() {
    return Chat::read_settings("money.llm", "MONEY_LLM", "prompt_parse", "MONEY_LLM_PROMPT_PARSE", 2500);
}

/// The model for receipt photos: MONEY_LLM_MODEL_VISION when set, else the main model.
inline std::string vision_model(const Chat::Settings& s) {
    if (!Config::is_initialized()) {
        return s.model;
    }
    const std::string v = Config::get().get<std::string>("money.llm.model_vision", "MONEY_LLM_MODEL_VISION", "");
    return v.empty() ? s.model : v;
}

}  // namespace Money::Llm
