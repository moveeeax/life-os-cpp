/**
 * @file MoodRepository.hpp
 * @brief mood_checkins: one check-in per owner and local day (score, tags,
 *        note). Every statement filters by owner.
 */

#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/SqlErrors.hpp"
#include "repositories/SqlTime.hpp"
#include "repositories/habits/HabitRepository.hpp"

namespace Repositories::Habits {

class MoodRepository {
public:
    struct Input {
        int score = 3;
        std::vector<std::string> tags;  // known tags only, checked by the caller
        std::string note;
    };

    /// The check-in of @p date, replaced when there is one already. @throws Invariant.
    nlohmann::json put(const std::string& owner, const std::string& date, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    txn.exec_params(
                        "INSERT INTO mood_checkins (owner_id, date, score, tags, note) "
                        "VALUES ($1::uuid, $2::date, $3, $4::text[], $5) "
                        "ON CONFLICT (owner_id, date) DO UPDATE SET score = EXCLUDED.score, tags = EXCLUDED.tags, "
                        " note = EXCLUDED.note",
                        owner,
                        date,
                        in.score,
                        pg_array(in.tags),
                        in.note);
                    auto r = txn.exec_params("SELECT row_to_json(t)::text FROM (SELECT " + std::string(kColumns) +
                                                 " FROM mood_checkins WHERE owner_id = $1::uuid AND date = $2::date) t",
                                             owner,
                                             date);
                    return nlohmann::json::parse(r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// Removes the check-in of @p date; false when there was none.
    bool remove(const std::string& owner, const std::string& date) {
        return Database::get().execute_write([&](auto& txn) {
            return !txn.exec_params(
                           "DELETE FROM mood_checkins WHERE owner_id = $1::uuid AND date = $2::date RETURNING date",
                           owner,
                           date)
                        .empty();
        });
    }

    /// The check-ins dated @p from..@p to (inclusive), oldest first.
    nlohmann::json list(const std::string& owner, const std::string& from, const std::string& to) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t ORDER BY t.date), '[]'::json)::text FROM (SELECT " +
                                         std::string(kColumns) +
                                         " FROM mood_checkins WHERE owner_id = $1::uuid "
                                         " AND date BETWEEN $2::date AND $3::date) t",
                                     owner,
                                     from,
                                     to);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

private:
    static constexpr const char* kColumns = "date::text AS date, score, tags, note, " SQL_ISO_UTC(
        "created_at") " AS created_at, " SQL_ISO_UTC("updated_at") " AS updated_at";

    /// A text[] literal of known tags (plain lower-case words, nothing to escape).
    static std::string pg_array(const std::vector<std::string>& tags) {
        std::string out = "{";
        for (const auto& t : tags) {
            if (out.size() > 1) {
                out += ',';
            }
            out += t;
        }
        return out + "}";
    }
};

}  // namespace Repositories::Habits
