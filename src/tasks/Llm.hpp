/**
 * @file Llm.hpp
 * @brief Settings of the tasks_parse job. TASKS_LLM_* when complete; else the
 *        money provider (MONEY_LLM_*) with the tasks prompt, so prod shares
 *        one key. Without TASKS_LLM_PROMPT_PARSE there is no parse at all.
 */

#pragma once

#include <optional>
#include <string>

#include "llm/Chat.hpp"
#include "utils/Config.hpp"

namespace Tasks::Llm {

inline std::optional<Chat::Settings> parse_settings() {
    if (auto own = Chat::read_settings("tasks.llm", "TASKS_LLM", "prompt_parse", "TASKS_LLM_PROMPT_PARSE", 1500)) {
        return own;
    }
    if (!Config::is_initialized()) {
        return std::nullopt;
    }
    const std::string prompt = Config::get().get<std::string>("tasks.llm.prompt_parse", "TASKS_LLM_PROMPT_PARSE", "");
    auto shared = Chat::read_settings("money.llm", "MONEY_LLM", "prompt_parse", "MONEY_LLM_PROMPT_PARSE", 1500);
    if (prompt.empty() || !shared.has_value()) {
        return std::nullopt;
    }
    shared->prompt = prompt;
    return shared;
}

}  // namespace Tasks::Llm
