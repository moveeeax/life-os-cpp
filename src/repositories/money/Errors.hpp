/**
 * @file Errors.hpp
 * @brief The errors of the money repositories and the SQLSTATE translation
 *        they share. An Invariant is one of the seven rules of the owner's
 *        model (spec §2) tripped by a write: the controller answers 400 with
 *        the message, never a 500.
 */

#pragma once

#include <string>
#include <string_view>

#include "repositories/RepoErrors.hpp"
#include "repositories/SqlErrors.hpp"

namespace Repositories::Money {

struct NotFound : NotFoundError {
    explicit NotFound(const char* what = "money_row") : NotFoundError(what) {}
};

struct Invariant : ValidationError {
    explicit Invariant(std::string message) : ValidationError("invariant", std::move(message)) {}
};

struct Duplicate : ConflictError {
    Duplicate() : ConflictError("duplicate", "a row with this external_id already exists") {}
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
        throw Invariant("the row breaks a rule of the money model");
    }
    if (sqlstate == "23503") {  // foreign_key_violation
        throw Invariant("the row refers to an account, category or currency that is not there");
    }
    if (sqlstate == "23505") {  // unique_violation: only external_id is unique among user-written columns
        throw Duplicate();
    }
    if (sqlstate == "22008" || sqlstate == "22007") {  // datetime_field_overflow, invalid_datetime_format
        throw InvalidDate();
    }
}

}  // namespace detail

}  // namespace Repositories::Money
