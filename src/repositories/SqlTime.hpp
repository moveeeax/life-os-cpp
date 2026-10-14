/**
 * @file SqlTime.hpp
 * @brief The one SQL spelling of an API timestamp: a timestamptz column as
 *        ISO 8601 text in UTC, `2026-10-14T09:30:00+00:00`.
 * @details Macros, not functions, so the fragment concatenates into the
 *          `static constexpr const char*` column lists the repositories keep:
 *          @code
 *            "SELECT id, " SQL_ISO_UTC("n.created_at") " AS created_at FROM notes n"
 *          @endcode
 *          The text a query sends is byte-for-byte what the 44 hand-written
 *          copies sent.
 */

#pragma once

/// The to_char pattern of SQL_ISO_UTC, quoted for SQL.
#define SQL_ISO_UTC_FORMAT "'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"'"

/// `to_char(<col> AT TIME ZONE 'UTC', <ISO pattern>)`; @p col is a string literal.
#define SQL_ISO_UTC(col) "to_char(" col " AT TIME ZONE 'UTC', " SQL_ISO_UTC_FORMAT ")"
