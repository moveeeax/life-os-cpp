/**
 * @file Register.cpp
 * @brief Body for src/parse/jobs/Register.hpp — compiled once into app_core.
 */

#include "parse/jobs/Register.hpp"

#include <string>

#include <nlohmann/json.hpp>

#include "jobs/Dispatcher.hpp"
#include "parse/jobs/ParseJob.hpp"
#include "repositories/parse/ParseJobRepository.hpp"

namespace UnifiedParse {

void register_jobs(Jobs::Dispatcher& d) {
    using nlohmann::json;
    d.register_handler(Jobs::UnifiedParse::kJobType,
                       [](const json& payload) { return Jobs::UnifiedParse::process_job(payload); });
    // When the queue gives the job up, its row is closed as failed (if still
    // queued or running) instead of waiting for a page that polls it.
    d.register_dead_letter(Jobs::UnifiedParse::kJobType, [](const json& payload, const std::string& error) {
        if (payload.contains("job_id") && payload["job_id"].is_string() &&
            !payload["job_id"].get<std::string>().empty())
            Repositories::UnifiedParse::ParseJobRepository().fail(
                payload["job_id"].get<std::string>(), "queue_gave_up", error);
    });
}

}  // namespace UnifiedParse
