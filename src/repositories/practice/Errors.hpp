/**
 * @file Errors.hpp
 * @brief The errors of the practice repositories and the SQLSTATE translation
 *        they share: a CHECK of the practice tables answers 400 `invariant`,
 *        a client that still has deals 409 `client_has_deals`.
 */

#pragma once

#include <string>
#include <string_view>

#include "repositories/RepoErrors.hpp"
#include "repositories/SqlErrors.hpp"

namespace Repositories::Practice {

struct NotFound : NotFoundError {
    explicit NotFound(const char* what) : NotFoundError(what) {}
};

struct Invariant : ValidationError {
    explicit Invariant(std::string message = "the row breaks a rule of the practice model")
        : ValidationError("invariant", std::move(message)) {}
};

struct ClientHasDeals : ConflictError {
    ClientHasDeals() : ConflictError("client_has_deals", "the client still has deals; delete or move them first") {}
};

/// An entry of a deal belongs to the deal's client.
struct NotOfClient : ValidationError {
    NotOfClient() : ValidationError("not_of_client", "the deal belongs to another client") {}
};

namespace detail {

using Repositories::detail::translate_sql;

/// CHECK violations as Invariant; a bad date as Invariant too.
inline void translate(std::string_view sqlstate) {
    if (sqlstate == "23514") {  // check_violation
        throw Invariant();
    }
    if (sqlstate == "22008" || sqlstate == "22007") {  // datetime_field_overflow, invalid_datetime_format
        throw Invariant("not a valid date");
    }
    if (Repositories::detail::is_fk_violation(sqlstate)) {  // a client or deal that is not there
        throw Invariant("the row refers to a client or deal that is not there");
    }
}

}  // namespace detail

}  // namespace Repositories::Practice
