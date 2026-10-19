/**
 * @file Register.cpp
 * @brief Body for src/fitness/jobs/Register.hpp — compiled once into app_core.
 */

#include "fitness/jobs/Register.hpp"

#include <string>

#include <nlohmann/json.hpp>

#include "fitness/jobs/SyncJob.hpp"
#include "jobs/Dispatcher.hpp"

namespace Fitness {

void register_jobs(Jobs::Dispatcher& d) {
    d.register_handler(Jobs::FitnessSync::kJobType,
                       [](const nlohmann::json& payload) { return Jobs::FitnessSync::process_job(payload); });
    d.register_dead_letter(Jobs::FitnessSync::kJobType, [](const nlohmann::json& payload, const std::string& error) {
        Jobs::FitnessSync::on_dead_letter(payload, error);
    });
}

}  // namespace Fitness
