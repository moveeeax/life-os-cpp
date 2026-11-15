/**
 * @file SqlTime.hpp
 * @brief The SQL spellings of an API timestamp: a timestamptz column as
 *        ISO 8601 text in UTC, `2026-10-14T09:30:00+00:00` (SQL_ISO_UTC) or
 *        `2026-10-14T09:30:00Z` (SQL_ISO_UTC_Z, the Ani, files, LeetCode and
 *        billing metrics answers). Both reach API clients as they are, so a
 *        column keeps the spelling it has.
 * @details Macros, not functions, so the fragment concatenates into the
 *          `static constexpr const char*` column lists the repositories keep:
 *          @code
 *            "SELECT id, " SQL_ISO_UTC("n.created_at") " AS created_at FROM notes n"
 *          @endcode
 *          The text a query sends is byte-for-byte what the 44 hand-written
 *          copies sent.
 */

#pragma once

#include <string>

/// The to_char pattern of SQL_ISO_UTC, quoted for SQL.
#define SQL_ISO_UTC_FORMAT "'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"'"

/// `to_char(<col> AT TIME ZONE 'UTC', <ISO pattern>)`; @p col is a string literal.
#define SQL_ISO_UTC(col) "to_char(" col " AT TIME ZONE 'UTC', " SQL_ISO_UTC_FORMAT ")"

/// The to_char pattern of SQL_ISO_UTC_Z, quoted for SQL.
#define SQL_ISO_UTC_Z_FORMAT "'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'"

/// SQL_ISO_UTC with a `Z` suffix in place of `+00:00`; @p col is a string literal.
#define SQL_ISO_UTC_Z(col) "to_char(" col " AT TIME ZONE 'UTC', " SQL_ISO_UTC_Z_FORMAT ")"

namespace Repositories {

/// Runtime twin of SQL_ISO_UTC (@p z false) and SQL_ISO_UTC_Z (@p z true)
/// for column lists built as std::string. @p col is SQL the repository
/// writes, never input.
inline std::string iso_utc(const std::string& col, bool z) {
    return "to_char(" + col + " AT TIME ZONE 'UTC', " + (z ? SQL_ISO_UTC_Z_FORMAT : SQL_ISO_UTC_FORMAT) + ")";
}

}  // namespace Repositories
