/**
 * @file ParseJob.hpp
 * @brief food_parse job handler: a text description to the LLM, the parsed
 *        lines into the job row.
 *
 * Payload: {job_id, owner_id}. The provider is whatever FOOD_LLM_* names;
 * the worker sends one OpenAI-compatible chat completion and validates the
 * answer against the schema in food/ParseAnswer.hpp. Nothing reaches the
 * diary here: the lines wait for the user's confirmation.
 *
 * Outcomes: a refusal of the provider (401/403), a bad answer, a missing
 * configuration or a disabled module close the job as failed without a
 * retry; a 429, a 5xx or a timeout throw, and the queue retries.
 */

#pragma once

#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "food/Http.hpp"
#include "food/Llm.hpp"
#include "food/ParseAnswer.hpp"
#include "jobs/RowJob.hpp"
#include "llm/Chat.hpp"
#include "repositories/food/GoalsRepository.hpp"
#include "repositories/food/ItemRepository.hpp"
#include "repositories/food/ParseJobRepository.hpp"
#include "utils/Modules.hpp"

namespace Jobs::FoodParse {

inline constexpr const char* kJobType = "food_parse";
/// How many of the user's items travel with the request, newest first.
inline constexpr long kItemsInPrompt = 200;

/// What the request carries about the user: their items (so the model prefers
/// them over guesses) and the profile note. The unified parse sends the same.
struct PromptContext {
    std::set<std::string> own_ids;
    nlohmann::json items = nlohmann::json::array();
    std::string profile_note;

    nlohmann::json message(const std::string& meal, const std::string& date) const {
        return {{"meal", meal}, {"date", date}, {"profile_note", profile_note}, {"items", items}};
    }
};

inline PromptContext prompt_context(const std::string& owner) {
    PromptContext ctx;
    for (const auto& item : Repositories::ItemRepository().for_prompt(owner, kItemsInPrompt)) {
        ctx.own_ids.insert(item["id"].get<std::string>());
        ctx.items.push_back(item);
    }
    if (const auto profile = Repositories::GoalsRepository().load(owner)) {
        ctx.profile_note = profile->value("profile_note", std::string());
    }
    return ctx;
}

inline nlohmann::json process_job(const nlohmann::json& payload) {
    const std::string job_id = payload.value("job_id", std::string());
    if (job_id.empty()) {
        throw std::invalid_argument("food_parse: payload has no job_id");
    }
    Repositories::ParseJobRepository jobs;
    Jobs::RowJob<Repositories::ParseJobRepository> job(kJobType, "food parse", "job_id", job_id, jobs, payload);

    if (!Core::food_enabled()) {
        return job.failed("food_disabled", "the food module is off on this worker");
    }
    const auto settings = Food::Llm::settings();
    if (!settings.has_value()) {
        return job.failed("not_configured", "FOOD_LLM_* settings are missing");
    }
    const auto row = jobs.load_for_worker(job_id);
    if (!row.has_value()) {
        // Nothing to update: the row is gone.
        return {{"job_id", job_id}, {"status", "missing"}};
    }
    if (const auto settled = job.claim()) {
        return *settled;
    }
    const std::string owner = row->value("owner_id", std::string());

    return job.run([&]() -> nlohmann::json {
        const auto ctx = prompt_context(owner);
        nlohmann::json user_message = ctx.message(row->value("meal", std::string()), row->value("date", std::string()));
        user_message["text"] = row->value("text", std::string());

        std::variant<Chat::Answer, Chat::Failure> asked;
        try {
            asked = Chat::ask(Food::Http::transport(), *settings, Chat::json_request(*settings, user_message.dump()));
        } catch (const Chat::Retryable& e) {
            return job.give_up_or_retry("provider_unavailable", e.what());
        }
        if (const auto* failure = std::get_if<Chat::Failure>(&asked)) {
            return job.failed(failure->code, failure->message);
        }
        const auto& answer = std::get<Chat::Answer>(asked);
        std::vector<Food::Parse::Line> lines;
        try {
            lines = Food::Parse::parse_answer(answer.content, ctx.own_ids);
        } catch (const Food::Parse::Invalid& e) {
            return job.failed("invalid_answer", e.what());
        }
        jobs.finish(job_id,
                    job.attempt(),
                    Food::Parse::to_json(lines),
                    answer.model,
                    answer.prompt_tokens,
                    answer.completion_tokens);
        return {{"job_id", job_id}, {"status", "done"}, {"lines", lines.size()}};
    });
}

}  // namespace Jobs::FoodParse
