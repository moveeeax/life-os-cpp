/**
 * @file Errors.hpp
 * @brief The errors of the goals repositories and the SQLSTATE translation
 *        they share: a CHECK of goal_items (a kind without its fields, a due
 *        date before the start) answers 400 `invariant`, never a 500.
 */

#pragma once

#include <string>
#include <string_view>

#include "repositories/RepoErrors.hpp"
#include "repositories/SqlErrors.hpp"

namespace Repositories::Goals {

struct NotFound : NotFoundError {
    explicit NotFound(const char* what = "goal_row") : NotFoundError(what) {}
};

struct Invariant : ValidationError {
    explicit Invariant(std::string message) : ValidationError("invariant", std::move(message)) {}
};

struct Duplicate : ConflictError {
    Duplicate() : ConflictError("duplicate", "a row with these keys already exists") {}
};

using Repositories::AlreadyAccepted;

using Repositories::InvalidDate;

namespace detail {

// The shared SQLSTATE wrapper, reachable as detail::translate_sql from this namespace.
using Repositories::detail::throw_on;
using Repositories::detail::translate_sql;

/// CHECK and FK violations of the goals tables as Invariant; a bad date as InvalidDate.
inline void translate(std::string_view sqlstate) {
    if (Repositories::detail::is_check_violation(sqlstate)) {
        throw Invariant("the row breaks a rule of the goals model");
    }
    if (Repositories::detail::is_fk_violation(sqlstate)) {  // foreign_key_violation, restrict_violation
        throw Invariant("the row refers to a goal or section that is not there");
    }
    if (Repositories::detail::is_unique_violation(sqlstate)) {  // unique_violation: one check-in per goal and date
        throw Duplicate();
    }
    if (Repositories::detail::is_bad_datetime(sqlstate)) {
        throw InvalidDate();
    }
}

}  // namespace detail

}  // namespace Repositories::Goals
