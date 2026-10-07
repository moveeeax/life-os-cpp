/**
 * @file TasksParseHandler.hpp
 * @brief tasks_parse job handler: one phrase to the LLM, the checked draft
 *        lines into the job row. Nothing becomes a task here: the user
 *        accepts the lines afterwards.
 *
 * Payload: {job_id, owner_id, max_attempts}. The request carries the phrase,
 * the hint date and the six areas; the open task titles of the last 30 days
 * mark a line that repeats one as a possible duplicate.
 *
 * Outcomes as in the money parse: a refusal, a bad answer, a missing
 * configuration or a disabled module close the job as failed; an outage
 * requeues it until the queue's last attempt, which closes it as failed.
 */

#pragma once

#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "core/Modules.hpp"
#include "jobs/Jobs.hpp"
#include "llm/Chat.hpp"
#include "net/Http.hpp"
#include "repositories/tasks/ParseJobRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "tasks/Fields.hpp"
#include "tasks/Llm.hpp"
#include "tasks/ParseAnswer.hpp"

namespace Jobs::TasksParse {

inline constexpr const char* kJobType = "tasks_parse";

inline nlohmann::json process_job(const nlohmann::json& payload) {
    if (!payload.contains("job_id") || !payload["job_id"].is_string() || payload["job_id"].get<std::string>().empty()) {
        throw std::invalid_argument("tasks_parse: payload has no job_id");
    }
    const std::string job_id = payload["job_id"].get<std::string>();
    Repositories::Tasks::ParseJobRepository jobs;
    const auto failed = [&](const std::string& code, const std::string& message) {
        jobs.fail(job_id, code, message);
        spdlog::warn("tasks parse {}: {}", job_id, code);
        return nlohmann::json{{"job_id", job_id}, {"status", "failed"}, {"error", code}};
    };

    if (!Core::tasks_enabled()) {
        return failed("tasks_disabled", "the tasks module is off on this worker");
    }
    const auto settings = ::Tasks::Llm::parse_settings();
    if (!settings.has_value()) {
        return failed("not_configured", "TASKS_LLM_PROMPT_PARSE or the provider settings are missing");
    }
    const auto job = jobs.load_for_worker(job_id);
    if (!job.has_value()) {
        return {{"job_id", job_id}, {"status", "missing"}};
    }
    const int attempt = jobs.start(job_id);
    if (attempt == 0) {
        return {{"job_id", job_id}, {"status", job->value("status", "unknown")}};
    }
    const std::string owner = job->value("owner_id", std::string());
    const int max_attempts =
        payload.contains("max_attempts") && payload["max_attempts"].is_number_integer()
            ? payload["max_attempts"].get<int>()
            : (Jobs::is_initialized() ? Jobs::get().default_max_retries() : Jobs::kDefaultMaxRetries);
    const auto give_up_or_retry = [&](const char* code, const std::string& what) -> nlohmann::json {
        if (attempt >= max_attempts) {
            return failed(code, what);
        }
        jobs.requeue(job_id);
        throw std::runtime_error(std::string("tasks_parse: ") + what);
    };

    try {
        const std::string hint_date = job->value("hint_date", std::string());
        const std::set<std::string> open_titles = Repositories::Tasks::TaskRepository().open_titles(owner);
        nlohmann::json areas = nlohmann::json::array();
        for (const auto a : ::Tasks::Fields::kAreas) {
            areas.push_back(std::string(a));
        }
        const nlohmann::json user_message{
            {"hint_date", hint_date}, {"areas", areas}, {"text", job->value("text", std::string())}};
        const std::string model = settings->model;
        const nlohmann::json request{{"model", model},
                                     {"messages",
                                      {{{"role", "system"}, {"content", settings->prompt}},
                                       {{"role", "user"}, {"content", user_message.dump()}}}},
                                     {"max_tokens", settings->max_tokens},
                                     {"temperature", 0},
                                     {"response_format", {{"type", "json_object"}}}};

        nlohmann::json body;
        std::string error_code;
        try {
            body = Chat::complete_adapting(Net::Http::transport(), *settings, request, error_code);
        } catch (const Chat::Retryable& e) {
            return give_up_or_retry("provider_unavailable", e.what());
        }
        if (!error_code.empty()) {
            return failed(error_code, Chat::provider_message(body));
        }
        const std::string content = Chat::content_of(body);
        if (content.empty()) {
            return failed("invalid_answer", "no message content");
        }
        std::vector<::Tasks::Parse::Line> lines;
        try {
            lines = ::Tasks::Parse::parse_answer(content, open_titles);
        } catch (const ::Tasks::Parse::Invalid& e) {
            return failed("invalid_answer", e.what());
        }
        jobs.finish(job_id,
                    ::Tasks::Parse::to_json(lines),
                    body.contains("model") && body["model"].is_string() ? body["model"].get<std::string>() : model,
                    Chat::usage(body, "prompt_tokens"),
                    Chat::usage(body, "completion_tokens"));
        return {{"job_id", job_id}, {"status", "done"}, {"lines", lines.size()}};
    } catch (const std::runtime_error& e) {
        // give_up_or_retry's own throw: the queue retries.
        if (std::string(e.what()).rfind("tasks_parse: ", 0) == 0) {
            throw;
        }
        return give_up_or_retry("internal_error", e.what());
    } catch (const std::exception& e) {
        return give_up_or_retry("internal_error", e.what());
    }
}

}  // namespace Jobs::TasksParse
