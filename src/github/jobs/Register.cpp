/**
 * @file Register.cpp
 * @brief Body for src/github/jobs/Register.hpp.
 */

#include "github/jobs/Register.hpp"

#include "github/Settings.hpp"
#include "github/jobs/Sync.hpp"
#include "jobs/Dispatcher.hpp"

namespace Github {

void register_jobs(Jobs::Dispatcher& d) {
    d.register_handler(kSyncJob, [](const nlohmann::json& payload) { return process_sync(payload); });
}

}  // namespace Github
