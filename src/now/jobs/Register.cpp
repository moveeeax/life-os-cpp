/**
 * @file Register.cpp
 * @brief Body for src/now/jobs/Register.hpp — compiled once into app_core.
 */

#include "now/jobs/Register.hpp"

#include <nlohmann/json.hpp>

#include "jobs/Dispatcher.hpp"
#include "now/jobs/MediaSweep.hpp"

namespace Now {

void register_jobs(Jobs::Dispatcher& d) {
    d.register_handler(Jobs::NowMediaSweep::kJobType,
                       [](const nlohmann::json& payload) { return Jobs::NowMediaSweep::process_job(payload); });
}

}  // namespace Now
