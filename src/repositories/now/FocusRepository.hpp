/**
 * @file FocusRepository.hpp
 * @brief day_focus: the one focus of a user's day — a task, a goal or typed
 *        text. The text holds the title at the time it was chosen, so the
 *        Now screen still shows it when the task is gone.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/RepoErrors.hpp"
#include "repositories/SqlErrors.hpp"

namespace Repositories::Now {

struct FocusNotFound : NotFoundError {
    FocusNotFound() : NotFoundError("focus") {}
};

struct FocusInvariant : ValidationError {
    FocusInvariant() : ValidationError("invariant", "the row breaks a rule of the day focus") {}
};

class FocusRepository {
public:
    /// {date, kind, ref_id, text, done}, or nullopt when the day has none.
    std::optional<nlohmann::json> get(const std::string& owner, const std::string& date) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, date); });
    }

    /// Sets or replaces the day's focus; a replaced focus is not done.
    nlohmann::json put(const std::string& owner,
                       const std::string& date,
                       const std::string& kind,
                       const std::optional<std::string>& ref_id,
                       const std::string& text) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    txn.exec_params(
                        "INSERT INTO day_focus (owner_id, date, kind, ref_id, text) "
                        "VALUES ($1::uuid, $2::date, $3, $4::uuid, $5) "
                        "ON CONFLICT (owner_id, date) DO UPDATE SET kind = EXCLUDED.kind, ref_id = EXCLUDED.ref_id, "
                        " text = EXCLUDED.text, done_at = NULL, updated_at = now()",
                        owner,
                        date,
                        kind,
                        ref_id,
                        text);
                    return *find_in(txn, owner, date);
                });
            },
            &translate);
    }

    /// @throws FocusNotFound when the day has no focus.
    nlohmann::json set_done(const std::string& owner, const std::string& date, bool done) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "UPDATE day_focus SET done_at = CASE WHEN $3::boolean THEN COALESCE(done_at, now()) END, updated_at = "
                "now() "
                "WHERE owner_id = $1::uuid AND date = $2::date RETURNING date",
                owner,
                date,
                done);
            if (r.empty()) {
                throw FocusNotFound();
            }
            return *find_in(txn, owner, date);
        });
    }

    /// @return whether there was a focus to clear.
    bool remove(const std::string& owner, const std::string& date) {
        return Database::get().execute_write([&](auto& txn) {
            return !txn.exec_params(
                           "DELETE FROM day_focus WHERE owner_id = $1::uuid AND date = $2::date RETURNING date",
                           owner,
                           date)
                        .empty();
        });
    }

private:
    static void translate(std::string_view sqlstate) {
        if (sqlstate == "23514") {  // check_violation
            throw FocusInvariant();
        }
    }

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& date) {
        auto r = txn.exec_params(
            "SELECT row_to_json(t) FROM (SELECT to_char(date, 'YYYY-MM-DD') AS date, kind, ref_id::text AS ref_id, "
            " text, done_at IS NOT NULL AS done FROM day_focus WHERE owner_id = $1::uuid AND date = $2::date) t",
            owner,
            date);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Now
