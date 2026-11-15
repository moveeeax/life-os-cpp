/**
 * @file JsonRows.hpp
 * @brief The JSON-in-SQL wrappers the owner-scoped life modules share. A
 *        module writes its SELECT with the column names and shapes of the
 *        API answer; one() wraps it in row_to_json, many() in json_agg, and
 *        both parse the one text cell that comes back. owns_in() is the
 *        owner check on a referenced row ("is this id one of yours?").
 *
 *        The domain-struct repositories (users, roles, billing) keep
 *        CrudBase.hpp instead.
 */

#pragma once

#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace Repositories::JsonRows {

/// One JSON object (row_to_json) of the first row of @p sql, or nullopt
/// when @p sql returns no row.
template <typename Txn, typename... Args>
std::optional<nlohmann::json> one(Txn& txn, const std::string& sql, Args&&... args) {
    auto r = txn.exec_params("SELECT row_to_json(t) FROM (" + sql + ") t", std::forward<Args>(args)...);
    if (r.empty()) {
        return std::nullopt;
    }
    return nlohmann::json::parse(r[0][0].template as<std::string>());
}

/// A JSON array (json_agg) of the rows of @p sql, in its order; [] when
/// there are none.
template <typename Txn, typename... Args>
nlohmann::json many(Txn& txn, const std::string& sql, Args&&... args) {
    auto r =
        txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (" + sql + ") t", std::forward<Args>(args)...);
    return nlohmann::json::parse(r[0][0].template as<std::string>());
}

/// True when @p id is a row of @p table that belongs to @p owner, or when
/// @p id is not set (nothing to check). @p table is a compile-time literal,
/// never input.
template <typename Txn>
bool owns_in(Txn& txn, const char* table, const std::string& owner, const std::optional<std::string>& id) {
    if (!id) {
        return true;
    }
    return !txn.exec_params(
                   std::string("SELECT 1 FROM ") + table + " WHERE owner_id = $1::uuid AND id = $2::uuid", owner, *id)
                .empty();
}

}  // namespace Repositories::JsonRows
