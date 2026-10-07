/**
 * @file NoteRepository.hpp
 * @brief task_notes: the inbox of thoughts and links that are not tasks yet.
 *        A note that became a task is archived with the task's id.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/tasks/Errors.hpp"

namespace Repositories::Tasks {

class NoteRepository {
public:
    nlohmann::json create(const std::string& owner, const std::string& text) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO task_notes (owner_id, text) VALUES ($1::uuid, $2) RETURNING id::text",
                        owner,
                        text);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// The owner's notes of one status, newest first.
    nlohmann::json list(const std::string& owner, const std::string& status) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                         " FROM task_notes WHERE owner_id = $1::uuid AND status = $2 "
                                         " ORDER BY created_at DESC) t",
                                     owner,
                                     status);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// @throws NotFound, Invariant.
    nlohmann::json update(const std::string& owner,
                          const std::string& id,
                          const std::optional<std::string>& text,
                          const std::optional<std::string>& status) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE task_notes SET text = COALESCE($3, text), status = COALESCE($4, status), "
                        " updated_at = now() WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        text,
                        status);
                    if (r.empty()) {
                        throw NotFound("task_note");
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM task_notes WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw NotFound("task_note");
            }
            return true;
        });
    }

    /// Whether the note is the owner's.
    template <typename Txn>
    static bool exists_in(Txn& txn, const std::string& owner, const std::string& id) {
        return !txn.exec_params("SELECT 1 FROM task_notes WHERE owner_id = $1::uuid AND id = $2::uuid", owner, id)
                    .empty();
    }

    /// The note became a task: archived, linked. A note of another owner is left alone.
    template <typename Txn>
    static void archive_in(Txn& txn, const std::string& owner, const std::string& note_id, const std::string& task_id) {
        txn.exec_params(
            "UPDATE task_notes SET status = 'archived', task_id = $3::uuid, updated_at = now() "
            "WHERE owner_id = $1::uuid AND id = $2::uuid",
            owner,
            note_id,
            task_id);
    }

    static constexpr const char* kColumns =
        "id, text, status, task_id, "
        " to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS created_at, "
        " to_char(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS updated_at";

private:
    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM task_notes WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Tasks
