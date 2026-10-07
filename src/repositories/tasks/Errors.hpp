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

struct AlreadyAccepted : ConflictError {
    AlreadyAccepted()
        : ConflictError("already_accepted", "the parse is not finished or its lines were accepted already") {}
};

struct InvalidDate : ValidationError {
    InvalidDate() : ValidationError("invalid_date", "not a valid date") {}
};

namespace detail {

// The shared SQLSTATE wrapper, reachable as detail::translate_sql from this namespace.
using Repositories::detail::throw_on;
using Repositories::detail::translate_sql;

/// CHECK and FK violations of the money tables as Invariant; a bad date as InvalidDate.
inline void translate(std::string_view sqlstate) {
    if (sqlstate == "23514") {  // check_violation
        throw Invariant("the row breaks a rule of the tasks model");
    }
    if (sqlstate == "23503") {  // foreign_key_violation
        throw Invariant("the row refers to a task or note that is not there");
    }
    if (sqlstate == "23505") {  // unique_violation: only external_id is unique among user-written columns
        throw Duplicate();
    }
    if (sqlstate == "22008" || sqlstate == "22007") {  // datetime_field_overflow, invalid_datetime_format
        throw InvalidDate();
    }
}

}  // namespace detail

}  // namespace Repositories::Tasks
