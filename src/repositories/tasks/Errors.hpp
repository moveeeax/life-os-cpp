/**
 * @file Errors.hpp
 * @brief The errors of the tasks repositories and the SQLSTATE translation
 *        they share: a CHECK of task_items (an area outside the six, a source
 *        kind without its ref) answers 400 `invariant`, never a 500.
 */

#pragma once

#include <string>
#include <string_view>

#include "repositories/RepoErrors.hpp"
#include "repositories/SqlErrors.hpp"

namespace Repositories::Tasks {

struct NotFound : NotFoundError {
    explicit NotFound(const char* what = "task_row") : NotFoundError(what) {}
};

struct Invariant : ValidationError {
    explicit Invariant(std::string message) : ValidationError("invariant", std::move(message)) {}
};

struct Duplicate : ConflictError {
    Duplicate() : ConflictError("duplicate", "a task with this external_id already exists") {}
};

using Repositories::AlreadyAccepted;

using Repositories::InvalidDate;

namespace detail {

// The shared SQLSTATE wrapper, reachable as detail::translate_sql from this namespace.
using Repositories::detail::throw_on;
using Repositories::detail::translate_sql;

/// CHECK and FK violations of the tasks tables as Invariant; a bad date as InvalidDate.
inline void translate(std::string_view sqlstate) {
    if (Repositories::detail::is_check_violation(sqlstate)) {
        throw Invariant("the row breaks a rule of the tasks model");
    }
    if (Repositories::detail::is_fk_violation(sqlstate)) {  // foreign_key_violation, restrict_violation
        throw Invariant("the row refers to a task or note that is not there");
    }
    // unique_violation: only external_id is unique among user-written columns
    if (Repositories::detail::is_unique_violation(sqlstate)) {
        throw Duplicate();
    }
    if (Repositories::detail::is_bad_datetime(sqlstate)) {
        throw InvalidDate();
    }
}

}  // namespace detail

}  // namespace Repositories::Tasks
