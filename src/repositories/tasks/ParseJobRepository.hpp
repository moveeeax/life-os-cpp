/**
 * @file ParseJobRepository.hpp
 * @brief task_parse_jobs: one phrase on its way through the LLM, the draft
 *        lines it produced, and whether the user accepted them. A phrase may
 *        come from an inbox note; accepting archives that note.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/tasks/Errors.hpp"
#include "repositories/tasks/NoteRepository.hpp"

namespace Repositories::Tasks {

class ParseJobRepository {
public:
    /// A row `running` longer than this was left by a worker that died.
    static constexpr const char* kStaleRunning = "10 minutes";

    /// @throws NotFound when @p note_id is not the owner's note.
    nlohmann::json create(const std::string& owner,
                          const std::string& text,
                          const std::string& hint_date,
                          const std::optional<std::string>& note_id) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    if (note_id.has_value() && !NoteRepository::exists_in(txn, owner, *note_id)) {
                        throw NotFound("task_note");
                    }
                    auto r = txn.exec_params(
                        "INSERT INTO task_parse_jobs (owner_id, text, hint_date, note_id) "
                        "VALUES ($1::uuid, $2, $3::date, $4::uuid) RETURNING id::text",
                        owner,
                        text,
                        hint_date,
                        note_id);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// The job as its owner sees it; nullopt for another owner's job.
    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

    /// What the worker needs; nullopt when the row is gone.
    std::optional<nlohmann::json> load_for_worker(const std::string& id) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT json_build_object('owner_id', owner_id, 'text', text, 'hint_date', hint_date::text, "
                " 'status', status) FROM task_parse_jobs WHERE id = $1::uuid",
                id);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// queued (or stale running) -> running, one more attempt; 0 when not claimable.
    int start(const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "UPDATE task_parse_jobs SET status = 'running', attempts = attempts + 1, started_at = now() "
                "WHERE id = $1::uuid AND (status = 'queued' OR (status = 'running' AND "
                " COALESCE(started_at, created_at) < now() - $2::interval)) RETURNING attempts",
                id,
                kStaleRunning);
            return r.empty() ? 0 : r[0][0].template as<int>();
        });
    }

    /// running -> queued, for an outage the queue will retry.
    void requeue(const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params("UPDATE task_parse_jobs SET status = 'queued' WHERE id = $1::uuid AND status = 'running'",
                            id);
            return true;
        });
    }

    void finish(const std::string& id,
                const nlohmann::json& lines,
                const std::string& model,
                std::optional<int> prompt_tokens,
                std::optional<int> completion_tokens) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE task_parse_jobs SET status = 'done', result = $2::jsonb, error = NULL, model = $3, "
                " prompt_tokens = $4, completion_tokens = $5, finished_at = now() WHERE id = $1::uuid",
                id,
                lines.dump(),
                model,
                prompt_tokens,
                completion_tokens);
            return true;
        });
    }

    /// @p error is stored as "<code>: <message>".
    void fail(const std::string& id, const std::string& code, const std::string& message) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE task_parse_jobs SET status = 'failed', error = $2, finished_at = now() WHERE id = $1::uuid",
                id,
                message.empty() ? code : code + ": " + message);
            return true;
        });
    }

    /// Jobs of the owner that are queued or running.
    long open_count(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT count(*) FROM task_parse_jobs WHERE owner_id = $1::uuid AND status IN ('queued', 'running')",
                owner);
            return r[0][0].template as<long>();
        });
    }

    /// The note a job came from, if any.
    template <typename Txn>
    static std::optional<std::string> note_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params(
            "SELECT note_id::text FROM task_parse_jobs WHERE owner_id = $1::uuid AND id = $2::uuid "
            " AND note_id IS NOT NULL",
            owner,
            id);
        if (r.empty()) {
            return std::nullopt;
        }
        return r[0][0].template as<std::string>();
    }

private:
    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params(
            "SELECT row_to_json(t) FROM (SELECT id, status, text, hint_date::text AS hint_date, note_id, result, "
            " error, model, prompt_tokens, completion_tokens, "
            " to_char(accepted_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS accepted_at, "
            " to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS created_at, "
            " to_char(finished_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS finished_at "
            " FROM task_parse_jobs WHERE owner_id = $1::uuid AND id = $2::uuid) t",
            owner,
            id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Tasks
