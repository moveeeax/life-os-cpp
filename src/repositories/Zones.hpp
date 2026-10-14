/**
 * @file Zones.hpp
 * @brief IANA time zones as the database knows them: whether a name is one,
 *        and today's date in it. The database's zone rules and clock are the
 *        ones every stored timestamp is read with, so a "today" computed here
 *        agrees with the rows.
 */

#pragma once

#include <optional>
#include <set>
#include <string>

#include "database/Database.hpp"

namespace Repositories::Zones {

/// Whether @p tz is a zone name PostgreSQL knows (read once per process).
inline bool is_zone(const std::string& tz) {
    static const std::set<std::string> zones = [] {
        return Database::get().execute_read([](auto& txn) {
            std::set<std::string> out;
            for (const auto& row : txn.exec("SELECT name FROM pg_timezone_names")) {
                out.insert(row[0].template as<std::string>());
            }
            return out;
        });
    }();
    return zones.count(tz) > 0;
}

/// Today in @p tz as YYYY-MM-DD; nullopt when @p tz is not a zone.
inline std::optional<std::string> today_in(const std::string& tz) {
    if (!is_zone(tz)) {
        return std::nullopt;
    }
    return Database::get().execute_read([&](auto& txn) {
        return txn.exec_params("SELECT to_char(now() AT TIME ZONE $1, 'YYYY-MM-DD')", tz)[0][0]
            .template as<std::string>();
    });
}

}  // namespace Repositories::Zones
