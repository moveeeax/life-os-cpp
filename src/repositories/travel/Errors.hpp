/**
 * @file Errors.hpp
 * @brief The errors of the travel repositories and their SQLSTATE translation:
 *        a CHECK of the travel tables (a bad country code, an end before the
 *        start) answers 400 `invariant`, never a 500.
 */

#pragma once

#include <set>
#include <string>
#include <string_view>

#include "repositories/RepoErrors.hpp"
#include "repositories/SqlErrors.hpp"

namespace Repositories::Travel {

struct NotFound : NotFoundError {
    explicit NotFound(const char* what = "travel_row") : NotFoundError(what) {}
};

struct Invariant : ValidationError {
    explicit Invariant(std::string message) : ValidationError("invariant", std::move(message)) {}
};

struct InvalidDate : ValidationError {
    InvalidDate() : ValidationError("invalid_date", "not a valid date or time") {}
};

namespace detail {

using Repositories::detail::translate_sql;

inline void translate(std::string_view sqlstate) {
    if (sqlstate == "23514") {  // check_violation
        throw Invariant("the row breaks a rule of the travel model");
    }
    if (sqlstate == "23503") {  // foreign_key_violation
        throw Invariant("the row refers to a trip that is not there");
    }
    if (sqlstate == "22008" || sqlstate == "22007") {  // datetime_field_overflow, invalid_datetime_format
        throw InvalidDate();
    }
}

/// A PostgreSQL text[] literal of codes already checked as [A-Z]{2} or SCHENGEN.
inline std::string pg_array(const std::set<std::string>& codes) {
    std::string out = "{";
    for (const auto& c : codes) {
        if (out.size() > 1) {
            out += ',';
        }
        out += c;
    }
    return out + "}";
}

}  // namespace detail

}  // namespace Repositories::Travel
