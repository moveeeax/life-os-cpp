/**
 * @file ParseApply.hpp
 * @brief The draft lines of a tasks parse created with apply, created as open
 *        tasks by the worker inside the transaction that closes the job.
 */

#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "repositories/tasks/TaskRepository.hpp"
#include "tasks/ParseAnswer.hpp"

namespace Jobs::TasksParse {

/// Creates @p lines as open tasks of @p owner inside @p txn; returns `[{id}]`.
template <typename Txn>
nlohmann::json apply_in(Txn& txn, const std::string& owner, const std::vector<::Tasks::Parse::Line>& lines) {
    using Repositories::Tasks::TaskRepository;
    nlohmann::json out = nlohmann::json::array();
    for (const auto& l : lines) {
        TaskRepository::Input in;
        in.title = l.title;
        in.area = l.area;
        in.effort = l.effort;
        in.due = l.due;
        in.next_step = l.next_step;
        in.status = "open";
        out.push_back({{"id", TaskRepository::create_in(txn, owner, in)}});
    }
    return out;
}

}  // namespace Jobs::TasksParse
