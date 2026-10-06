/**
 * @file ParseJobRepository.hpp
 * @brief Text descriptions sent to the LLM (table food_parse_jobs).
 *
 * A job row is created by the API when the user asks for a parse, claimed by
 * the worker (queued -> running once: a redelivered job finds it taken), and
 * closed with the parsed lines or an error code. The lines wait there until
 * the user confirms them into the diary.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories {

class ParseJobRepository {
public:
    /// A queued job; returns its row.
    nlohmann::json create(const std::string& owner,
                          const std::string& text,
                          const std::string& meal,
                          const std::string& date) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "INSERT INTO food_parse_jobs (owner_id, text, meal, date) VALUES ($1::uuid, $2, $3, $4::date) "
                "RETURNING id::text",
                owner,
                text,
                meal,
                date);
            return *find_in(txn, owner, r[0][0].template as<std::string>());
        });
    }

    /// The job as its owner sees it; nullopt for another owner's job.
    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

    /// What the worker needs: {owner_id, text, meal, date}; nullopt when the row is gone.
    std::optional<nlohmann::json> load_for_worker(const std::string& id) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT json_build_object('owner_id', owner_id, 'text', text, 'meal', meal, 'date', date::text, "
                " 'status', status) FROM food_parse_jobs WHERE id = $1::uuid",
                id);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// queued -> running. False when the job is not queued (taken or closed).
    /// queued -> running and one more attempt. Returns the attempt number
    /// (1 for the first run), or 0 when the row was not queued.
    int start(const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "UPDATE food_parse_jobs SET status = 'running', attempts = attempts + 1 "
                "WHERE id = $1::uuid AND status = 'queued' RETURNING attempts",
                id);
            return r.empty() ? 0 : r[0][0].template as<int>();
        });
    }

    /// running -> queued, for a provider outage the queue will retry.
    void requeue(const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params("UPDATE food_parse_jobs SET status = 'queued' WHERE id = $1::uuid AND status = 'running'",
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
                "UPDATE food_parse_jobs SET status = 'done', result = $2::jsonb, error = NULL, model = $3, "
                " prompt_tokens = $4, completion_tokens = $5, finished_at = now() WHERE id = $1::uuid",
                id,
                lines.dump(),
                model,
                prompt_tokens,
                completion_tokens);
            return true;
        });
    }

    /// @p error is "<code>: <message>"; the code is what the page reads.
    void fail(const std::string& id, const std::string& code, const std::string& message) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE food_parse_jobs SET status = 'failed', error = $2, finished_at = now() WHERE id = $1::uuid",
                id,
                message.empty() ? code : code + ": " + message);
            return true;
        });
    }

private:
    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params(
            "SELECT row_to_json(t) FROM (SELECT id, status, text, meal, date::text, result, error, model, "
            " prompt_tokens, completion_tokens, "
            " to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS created_at, "
            " to_char(finished_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS finished_at "
            " FROM food_parse_jobs WHERE owner_id = $1::uuid AND id = $2::uuid) t",
            owner,
            id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories
