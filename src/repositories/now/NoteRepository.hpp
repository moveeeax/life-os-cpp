/**
 * @file NoteRepository.hpp
 * @brief The note of a day on Now: one text per owner and date.
 */

#pragma once

#include <optional>
#include <string>
#include <utility>

#include "database/Database.hpp"

namespace Repositories::Now {

class NoteRepository {
public:
    std::optional<std::string> get(const std::string& owner, const std::string& date) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<std::string> {
            auto r =
                txn.exec_params("SELECT text FROM day_note WHERE owner_id = $1::uuid AND date = $2::date", owner, date);
            if (r.empty()) {
                return std::nullopt;
            }
            return r[0][0].template as<std::string>();
        });
    }

    /// The note of @p date and of the day before it.
    std::pair<std::optional<std::string>, std::optional<std::string>> with_yesterday(const std::string& owner,
                                                                                     const std::string& date) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT (SELECT text FROM day_note WHERE owner_id = $1::uuid AND date = $2::date), "
                " (SELECT text FROM day_note WHERE owner_id = $1::uuid AND date = $2::date - 1)",
                owner,
                date);
            const auto at = [&](int i) {
                return r[0][i].is_null() ? std::nullopt : std::optional(r[0][i].template as<std::string>());
            };
            return std::pair{at(0), at(1)};
        });
    }

    /// Text already checked (1..2000 characters).
    void put(const std::string& owner, const std::string& date, const std::string& text) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO day_note (owner_id, date, text) VALUES ($1::uuid, $2::date, $3) "
                "ON CONFLICT (owner_id, date) DO UPDATE SET text = EXCLUDED.text, updated_at = now()",
                owner,
                date,
                text);
            return true;
        });
    }

    void remove(const std::string& owner, const std::string& date) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params("DELETE FROM day_note WHERE owner_id = $1::uuid AND date = $2::date", owner, date);
            return true;
        });
    }
};

}  // namespace Repositories::Now
