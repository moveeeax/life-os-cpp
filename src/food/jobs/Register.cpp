/**
 * @file Register.cpp
 * @brief Body for src/food/jobs/Register.hpp — compiled once into app_core.
 */

#include "food/jobs/Register.hpp"

#include <string>

#include <nlohmann/json.hpp>

#include "food/jobs/ParseJob.hpp"
#include "jobs/Dispatcher.hpp"
#include "repositories/food/ParseJobRepository.hpp"

namespace Food {

void register_jobs(Jobs::Dispatcher& d) {
    using nlohmann::json;
    // A text description to the LLM; a bad answer closes the job, a provider
    // outage throws for a retry.
    d.register_handler(Jobs::FoodParse::kJobType,
                       [](const json& payload) { return Jobs::FoodParse::process_job(payload); });
    // When the queue gives the job up, its row is closed as failed (if still
    // queued or running) instead of waiting for a page that polls it.
    d.register_dead_letter(Jobs::FoodParse::kJobType, [](const json& payload, const std::string& error) {
        if (payload.contains("job_id") && payload["job_id"].is_string() &&
            !payload["job_id"].get<std::string>().empty())
            Repositories::ParseJobRepository().fail(payload["job_id"].get<std::string>(), "queue_gave_up", error);
    });
}

}  // namespace Food
