/**
 * @file ParseJob.hpp
 * @brief tasks_parse job handler: one phrase to the LLM, the checked draft
 *        lines into the job row. The user accepts the lines afterwards,
 *        unless the job was created with apply: then the worker creates
 *        them as open tasks itself (tasks/jobs/ParseApply.hpp).
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
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "jobs/RowJob.hpp"
#include "llm/Chat.hpp"
#include "net/Http.hpp"
#include "repositories/tasks/ParseJobRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "tasks/Fields.hpp"
#include "tasks/Llm.hpp"
#include "tasks/ParseAnswer.hpp"
#include "tasks/jobs/ParseApply.hpp"
#include "utils/Modules.hpp"

namespace Jobs::TasksParse {

inline constexpr const char* kJobType = "tasks_parse";

/// What the request carries besides the phrase: the hint date and the six
/// areas. The unified parse sends the same.
inline nlohmann::json prompt_context(const std::string& hint_date) {
    nlohmann::json areas = nlohmann::json::array();
    for (const auto a : ::Tasks::Fields::kAreas) {
        areas.push_back(std::string(a));
    }
    return {{"hint_date", hint_date}, {"areas", areas}};
}

inline nlohmann::json process_job(const nlohmann::json& payload) {
    if (!payload.contains("job_id") || !payload["job_id"].is_string() || payload["job_id"].get<std::string>().empty()) {
        throw std::invalid_argument("tasks_parse: payload has no job_id");
    }
    Repositories::Tasks::ParseJobRepository jobs;
    Jobs::RowJob<Repositories::Tasks::ParseJobRepository> job(
        kJobType, "tasks parse", "job_id", payload["job_id"].get<std::string>(), jobs, payload);

    if (!Core::tasks_enabled()) {
        return job.failed("tasks_disabled", "the tasks module is off on this worker");
    }
    const auto settings = ::Tasks::Llm::parse_settings();
    if (!settings.has_value()) {
        return job.failed("not_configured", "TASKS_LLM_PROMPT_PARSE or the provider settings are missing");
    }
    const auto row = jobs.load_for_worker(job.id());
    if (!row.has_value()) {
        return {{"job_id", job.id()}, {"status", "missing"}};
    }
    if (const auto settled = job.claim()) {
        return *settled;
    }
    const std::string owner = row->value("owner_id", std::string());

    return job.run([&]() -> nlohmann::json {
        const std::string hint_date = row->value("hint_date", std::string());
        const std::set<std::string> open_titles = Repositories::Tasks::TaskRepository().open_titles(owner);
        nlohmann::json user_message = prompt_context(hint_date);
        user_message["text"] = row->value("text", std::string());

        std::variant<Chat::Answer, Chat::Failure> asked;
        try {
            asked = Chat::ask(Net::Http::transport(), *settings, Chat::json_request(*settings, user_message.dump()));
        } catch (const Chat::Retryable& e) {
            return job.give_up_or_retry("provider_unavailable", e.what());
        }
        if (const auto* failure = std::get_if<Chat::Failure>(&asked)) {
            return job.failed(failure->code, failure->message);
        }
        const auto& answer = std::get<Chat::Answer>(asked);
        std::vector<::Tasks::Parse::Line> lines;
        try {
            lines = ::Tasks::Parse::parse_answer(answer.content, open_titles);
        } catch (const ::Tasks::Parse::Invalid& e) {
            return job.failed("invalid_answer", e.what());
        }
        // A job created with apply creates its tasks in the transaction that closes it.
        jobs.finish_applying(job.id(),
                             job.attempt(),
                             ::Tasks::Parse::to_json(lines),
                             answer.model,
                             answer.prompt_tokens,
                             answer.completion_tokens,
                             [&](auto& txn, const std::string& row_owner) { return apply_in(txn, row_owner, lines); });
        return {{"job_id", job.id()}, {"status", "done"}, {"lines", lines.size()}};
    });
}

}  // namespace Jobs::TasksParse
