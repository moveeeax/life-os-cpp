/**
 * @file Register.cpp
 * @brief Body for src/leetcode/jobs/Register.hpp.
 */

#include "leetcode/jobs/Register.hpp"

#include "jobs/Dispatcher.hpp"
#include "leetcode/Sync.hpp"

namespace LeetCode {

void register_jobs(Jobs::Dispatcher& d) {
    d.register_handler(kSyncJob, [](const nlohmann::json& payload) { return process_sync(payload); });
}

}  // namespace LeetCode
