/**
 * @file Register.cpp
 * @brief Body for src/files/jobs/Register.hpp — compiled once into app_core.
 */

#include "files/jobs/Register.hpp"

#include <nlohmann/json.hpp>

#include "files/jobs/Sweep.hpp"
#include "jobs/Dispatcher.hpp"

namespace Files {

void register_jobs(Jobs::Dispatcher& d) {
    d.register_handler(Jobs::FilesSweep::kJobType,
                       [](const nlohmann::json& payload) { return Jobs::FilesSweep::process_job(payload); });
}

}  // namespace Files
