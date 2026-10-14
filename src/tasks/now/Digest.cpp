/**
 * @file Digest.cpp
 * @brief Body for src/tasks/now/Digest.hpp — compiled once into app_core.
 */

#include "tasks/now/Digest.hpp"

#include <nlohmann/json.hpp>

#include "now/Today.hpp"
#include "repositories/tasks/InboxRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "tasks/Agenda.hpp"

namespace Tasks {

nlohmann::json digest(const std::string& owner, const std::string& day, const std::string& tz) {
    const auto groups = Agenda::group(Repositories::Tasks::TaskRepository().agenda_items(owner, day, tz), day);
    return ::Now::Today::tasks_block(groups, day, Repositories::Tasks::InboxRepository().count(owner, "inbox"));
}

}  // namespace Tasks
