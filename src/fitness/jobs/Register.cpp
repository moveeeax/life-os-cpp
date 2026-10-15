/**
 * @file Register.cpp
 * @brief Body for src/fitness/jobs/Register.hpp — compiled once into app_core.
 */

#include "fitness/jobs/Register.hpp"

#include <nlohmann/json.hpp>

#include "fitness/jobs/SyncJob.hpp"
#include "jobs/Dispatcher.hpp"

namespace Fitness {

void register_jobs(Jobs::Dispatcher& d) {
    d.register_handler(Jobs::FitnessSync::kJobType,
                       [](const nlohmann::json& payload) { return Jobs::FitnessSync::process_job(payload); });
}

}  // namespace Fitness
