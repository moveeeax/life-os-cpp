/**
 * @file FocusTask.hpp
 * @brief A focus of the day whose ref is a task: tick the focus and close or
 *        reopen the task in one transaction. A focus ticked without its task
 *        (or the other way round) is a lie the Now screen would show.
 */

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/now/FocusRepository.hpp"
#include "repositories/tasks/Errors.hpp"
#include "repositories/tasks/TaskRepository.hpp"

namespace Repositories::Now {

/// FocusRepository::set_done_in and TaskRepository::update_in (with @p patch on
/// @p task_id) in one transaction, the task's SQLSTATEs translated as
/// TaskRepository::update does. Returns the focus row.
/// @throws FocusNotFound, Tasks::NotFound and the task's invariant errors.
inline nlohmann::json set_done_with_task(const std::string& owner,
                                         const std::string& date,
                                         bool done,
                                         const std::string& task_id,
                                         const ::Repositories::Tasks::TaskRepository::Patch& patch) {
    return ::Repositories::Tasks::detail::translate_sql(
        [&] {
            return Database::get().execute_write([&](auto& txn) {
                ::Repositories::Tasks::TaskRepository::update_in(txn, owner, task_id, patch);
                return FocusRepository::set_done_in(txn, owner, date, done);
            });
        },
        &::Repositories::Tasks::detail::translate);
}

}  // namespace Repositories::Now
