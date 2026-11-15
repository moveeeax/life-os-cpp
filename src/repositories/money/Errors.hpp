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

using Repositories::AlreadyAccepted;

/// A delete that lost a race: a row referencing the account or category
/// arrived after the "is it used" check of the same delete.
struct InUse : ConflictError {
    InUse() : ConflictError("in_use", "rows were added to it meanwhile; it was not deleted") {}
};

/// A currency's decimals lowered while an account, a budget or a receipt in it
/// keeps amounts at the old scale (check_scale_in would refuse them afterwards).
struct DecimalsInUse : ConflictError {
    DecimalsInUse()
        : ConflictError("decimals_in_use",
                        "an account, a budget or a receipt in this currency keeps its decimals; fewer decimals "
                        "would leave their amounts unrepresentable") {}
};

using Repositories::InvalidDate;

namespace detail {

// The shared SQLSTATE wrapper, reachable as detail::translate_sql from this namespace.
using Repositories::detail::throw_on;
using Repositories::detail::translate_sql;

/// CHECK and FK violations of the money tables as Invariant; a bad date as InvalidDate.
inline void translate(std::string_view sqlstate) {
    if (Repositories::detail::is_check_violation(sqlstate)) {
        throw Invariant("the row breaks a rule of the money model");
    }
    if (Repositories::detail::is_fk_violation(sqlstate)) {  // foreign_key_violation, restrict_violation
        throw Invariant("the row refers to an account, category or currency that is not there");
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

}  // namespace Repositories::Money
