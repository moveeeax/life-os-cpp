/**
 * @file Register.cpp
 * @brief Body for src/tasks/jobs/Register.hpp — compiled once into app_core.
 */

#include "tasks/jobs/Register.hpp"

#include <string>

#include <nlohmann/json.hpp>

#include "jobs/Dispatcher.hpp"
#include "repositories/tasks/ParseJobRepository.hpp"
#include "tasks/jobs/ParseJob.hpp"

namespace Tasks {

void register_jobs(Jobs::Dispatcher& d) {
    using nlohmann::json;
    d.register_handler(Jobs::TasksParse::kJobType,
                       [](const json& payload) { return Jobs::TasksParse::process_job(payload); });
    // When the queue gives the job up, its row is closed as failed (if still
    // queued or running) instead of waiting for a page that polls it.
    d.register_dead_letter(Jobs::TasksParse::kJobType, [](const json& payload, const std::string& error) {
        if (payload.contains("job_id") && payload["job_id"].is_string() &&
            !payload["job_id"].get<std::string>().empty())
            Repositories::Tasks::ParseJobRepository().fail(
                payload["job_id"].get<std::string>(), "queue_gave_up", error);
    });
}

}  // namespace Tasks
