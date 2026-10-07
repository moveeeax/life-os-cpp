/**
 * @file FoodParseHandler.hpp
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

#include <exception>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "core/Modules.hpp"
#include "food/Http.hpp"
#include "food/Llm.hpp"
#include "food/ParseAnswer.hpp"
#include "jobs/Jobs.hpp"
#include "llm/Chat.hpp"
#include "repositories/food/GoalsRepository.hpp"
#include "repositories/food/ItemRepository.hpp"
#include "repositories/food/ParseJobRepository.hpp"

namespace Jobs::FoodParse {

inline constexpr const char* kJobType = "food_parse";
/// How many of the user's items travel with the request, newest first.
inline constexpr long kItemsInPrompt = 200;

namespace detail {
// The shared chat call (llm/Chat.hpp), under the names this handler used before.
using Chat::adapt_request;
using Chat::complete;
using Chat::provider_message;
using Chat::Retryable;
using Chat::usage;
}  // namespace detail

inline nlohmann::json process_job(const nlohmann::json& payload) {
    const std::string job_id = payload.value("job_id", std::string());
    Repositories::ParseJobRepository jobs;
    if (job_id.empty()) {
        throw std::invalid_argument("food_parse: payload has no job_id");
    }
    const auto failed = [&](const std::string& code, const std::string& message) {
        jobs.fail(job_id, code, message);
        spdlog::warn("food parse {}: {}", job_id, code);
        return nlohmann::json{{"job_id", job_id}, {"status", "failed"}, {"error", code}};
    };

    if (!Core::food_enabled()) {
        return failed("food_disabled", "the food module is off on this worker");
    }
    const auto settings = Food::Llm::settings();
    if (!settings.has_value()) {
        return failed("not_configured", "FOOD_LLM_* settings are missing");
    }
    const auto job = jobs.load_for_worker(job_id);
    if (!job.has_value()) {
        // Nothing to update: the row is gone.
        return {{"job_id", job_id}, {"status", "missing"}};
    }
    const int attempt = jobs.start(job_id);
    if (attempt == 0) {
        // A redelivery of a job that already ran: its journal stays as it is.
        return {{"job_id", job_id}, {"status", job->value("status", "unknown")}};
    }
    const std::string owner = job->value("owner_id", std::string());
    // The job carries the retry limit it was submitted with; the worker's own
    // setting is the fallback for a job enqueued by an older API.
    const int max_attempts = payload.value(
        "max_attempts", Jobs::is_initialized() ? Jobs::get().default_max_retries() : Jobs::kDefaultMaxRetries);
    // A row that is `running` must end this call as done, failed or queued
    // again: the page polls it, and a redelivery cannot claim it.
    const auto give_up_or_retry = [&](const char* code, const std::string& what) -> nlohmann::json {
        if (attempt >= max_attempts) {
            return failed(code, what);
        }
        jobs.requeue(job_id);
        throw std::runtime_error(std::string("food_parse: ") + what);
    };

    try {
        // The user's items, so the model prefers them over guesses.
        nlohmann::json items = nlohmann::json::array();
        std::set<std::string> own_ids;
        for (const auto& item : Repositories::ItemRepository().for_prompt(owner, kItemsInPrompt)) {
            own_ids.insert(item["id"].get<std::string>());
            items.push_back(item);
        }
        std::string profile_note;
        if (const auto profile = Repositories::GoalsRepository().load(owner)) {
            profile_note = profile->value("profile_note", std::string());
        }

        const nlohmann::json user_message{{"text", job->value("text", std::string())},
                                          {"meal", job->value("meal", std::string())},
                                          {"date", job->value("date", std::string())},
                                          {"profile_note", profile_note},
                                          {"items", items}};
        nlohmann::json request{{"model", settings->model},
                               {"messages",
                                {{{"role", "system"}, {"content", settings->prompt}},
                                 {{"role", "user"}, {"content", user_message.dump()}}}},
                               {"max_tokens", settings->max_tokens},
                               {"temperature", 0},
                               {"response_format", {{"type", "json_object"}}}};

        auto& transport = Food::Http::transport();
        nlohmann::json body;
        std::string error_code;
        try {
            body = detail::complete(transport, *settings, request, error_code);
            // Three optional parameters, so at most three adapted retries.
            for (int round = 0; round < 3 && error_code == "provider_error_400" && detail::adapt_request(request, body);
                 ++round) {
                body = detail::complete(transport, *settings, request, error_code);
            }
        } catch (const detail::Retryable& e) {
            return give_up_or_retry("provider_unavailable", e.what());
        }
        if (!error_code.empty()) {
            return failed(error_code, detail::provider_message(body));
        }

        std::string content;
        if (body.contains("choices") && body["choices"].is_array() && !body["choices"].empty() &&
            body["choices"][0].is_object() && body["choices"][0].contains("message") &&
            body["choices"][0]["message"].is_object() && body["choices"][0]["message"].contains("content") &&
            body["choices"][0]["message"]["content"].is_string()) {
            content = body["choices"][0]["message"]["content"].get<std::string>();
        }
        if (content.empty()) {
            return failed("invalid_answer", "no message content");
        }
        std::vector<Food::Parse::Line> lines;
        try {
            lines = Food::Parse::parse_answer(content, own_ids);
        } catch (const Food::Parse::Invalid& e) {
            return failed("invalid_answer", e.what());
        }
        const std::string model =
            body.contains("model") && body["model"].is_string() ? body["model"].get<std::string>() : settings->model;
        jobs.finish(job_id,
                    Food::Parse::to_json(lines),
                    model,
                    detail::usage(body, "prompt_tokens"),
                    detail::usage(body, "completion_tokens"));
        return {{"job_id", job_id}, {"status", "done"}, {"lines", lines.size()}};
    } catch (const std::exception& e) {
        // A database error or an unexpected shape: the row is still `running`
        // and must not stay so.
        return give_up_or_retry("internal_error", e.what());
    }
}

}  // namespace Jobs::FoodParse
