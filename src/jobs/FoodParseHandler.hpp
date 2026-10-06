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

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "core/Modules.hpp"
#include "food/Http.hpp"
#include "food/Llm.hpp"
#include "food/ParseAnswer.hpp"
#include "repositories/food/GoalsRepository.hpp"
#include "repositories/food/ItemRepository.hpp"
#include "repositories/food/ParseJobRepository.hpp"

namespace Jobs::FoodParse {

inline constexpr const char* kJobType = "food_parse";
/// How many of the user's items travel with the request, newest first.
inline constexpr long kItemsInPrompt = 200;

namespace detail {

/// The provider did not take the request; the queue retries.
struct Retryable : std::runtime_error {
    explicit Retryable(const std::string& what) : std::runtime_error(what) {}
};

/// One chat completion. Returns the response body as JSON.
/// @throws Retryable on 429, 5xx, a timeout or a body that is not JSON.
inline nlohmann::json complete(Food::Http::Transport& transport,
                               const Food::Llm::Settings& s,
                               const nlohmann::json& request,
                               std::string& error_code) {
    Food::Http::Response response;
    try {
        response = transport.post_json(s.base_url + "/chat/completions",
                                       request.dump(),
                                       {{"Authorization", "Bearer " + s.api_key}, {"Accept", "application/json"}},
                                       s.timeout_seconds);
    } catch (const Food::Http::TransportError& e) {
        throw Retryable(e.what());
    }
    if (response.status == 429 || response.status >= 500) {
        throw Retryable("provider answered HTTP " + std::to_string(response.status));
    }
    nlohmann::json body = nlohmann::json::parse(response.body, nullptr, /*allow_exceptions=*/false);
    if (response.status == 401 || response.status == 403) {
        error_code = "provider_refused";
        return nlohmann::json();
    }
    if (response.status != 200) {
        // A 400 may be the provider refusing response_format; the caller decides.
        error_code = "provider_error_" + std::to_string(response.status);
        return body.is_discarded() ? nlohmann::json() : body;
    }
    if (body.is_discarded() || !body.is_object()) {
        throw Retryable("provider answered something that is not JSON");
    }
    error_code.clear();
    return body;
}

inline bool mentions_response_format(const nlohmann::json& body) {
    return body.is_object() && body.dump().find("response_format") != std::string::npos;
}

inline std::optional<int> usage(const nlohmann::json& body, const char* key) {
    if (body.contains("usage") && body["usage"].is_object() && body["usage"].contains(key) &&
        body["usage"][key].is_number_integer()) {
        return body["usage"][key].get<int>();
    }
    return std::nullopt;
}

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
    if (!jobs.start(job_id)) {
        // A redelivery of a job that already ran: its journal stays as it is.
        return {{"job_id", job_id}, {"status", job->value("status", "unknown")}};
    }
    const std::string owner = job->value("owner_id", std::string());

    // The user's items, so the model prefers them over guesses.
    nlohmann::json items = nlohmann::json::array();
    std::set<std::string> own_ids;
    try {
        for (const auto& item : Repositories::ItemRepository().list(owner, "", false, kItemsInPrompt, 0).rows) {
            own_ids.insert(item["id"].get<std::string>());
            items.push_back({{"id", item["id"]},
                             {"name", item["name"]},
                             {"brand", item["brand"]},
                             {"per", item["per"]},
                             {"kcal", item["kcal"]},
                             {"protein_g", item["protein_g"]},
                             {"fat_g", item["fat_g"]},
                             {"carbs_g", item["carbs_g"]}});
        }
    } catch (const std::exception& e) {
        return failed("items_unavailable", e.what());
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
    nlohmann::json request{
        {"model", settings->model},
        {"messages",
         {{{"role", "system"}, {"content", settings->prompt}}, {{"role", "user"}, {"content", user_message.dump()}}}},
        {"max_tokens", settings->max_tokens},
        {"temperature", 0},
        {"response_format", {{"type", "json_object"}}}};

    auto& transport = Food::Http::transport();
    nlohmann::json body;
    std::string error_code;
    try {
        body = detail::complete(transport, *settings, request, error_code);
        if (error_code.rfind("provider_error_", 0) == 0 && detail::mentions_response_format(body)) {
            // The provider does not know response_format: once more without it.
            request.erase("response_format");
            body = detail::complete(transport, *settings, request, error_code);
        }
    } catch (const detail::Retryable& e) {
        // Back to queued: the queue's retry runs this job again.
        jobs.requeue(job_id);
        throw std::runtime_error(std::string("food_parse: ") + e.what());
    }
    if (!error_code.empty()) {
        return failed(error_code, "");
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
    try {
        const auto lines = Food::Parse::parse_answer(content, own_ids);
        jobs.finish(job_id,
                    Food::Parse::to_json(lines),
                    body.value("model", settings->model),
                    detail::usage(body, "prompt_tokens"),
                    detail::usage(body, "completion_tokens"));
        return {{"job_id", job_id}, {"status", "done"}, {"lines", lines.size()}};
    } catch (const Food::Parse::Invalid& e) {
        return failed("invalid_answer", e.what());
    }
}

}  // namespace Jobs::FoodParse
