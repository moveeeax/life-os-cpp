/**
 * @file ParseJobRepository.hpp
 * @brief Text descriptions sent to the LLM (table food_parse_jobs).
 *
 * A job row is created by the API when the user asks for a parse, claimed by
 * the worker (see JobRows.hpp for the claim and the stale rule), and
 * closed with the parsed lines or an error code. The lines wait there until
 * the user confirms them into the diary.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/JobRows.hpp"
#include "repositories/SqlTime.hpp"

namespace Repositories {

class ParseJobRepository {
public:
    static constexpr const char* kTable = "food_parse_jobs";

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

    /// queued (or stale running) -> running, one more attempt; 0 when not claimable.
    int start(const std::string& id) { return JobRows::start(kTable, id); }

    /// The row's status now; nullopt when the row is gone.
    std::optional<std::string> status(const std::string& id) { return JobRows::status(kTable, id); }

    /// Jobs of the owner that are queued or running (a stale running row is not counted).
    long open_count(const std::string& owner) { return JobRows::open_count(kTable, owner); }

    /// running -> queued, for an outage the queue will retry; only by the holder of @p attempt.
    void requeue(const std::string& id, int attempt) { JobRows::requeue(kTable, id, attempt); }

    /// Milliseconds until a running row may be claimed again; 0 when it is not running.
    long long stale_in_ms(const std::string& id) { return JobRows::stale_in_ms(kTable, id); }

    /// Only the holder of @p attempt (what start() returned) writes the result.
    void finish(const std::string& id,
                int attempt,
                const nlohmann::json& lines,
                const std::string& model,
                std::optional<int> prompt_tokens,
                std::optional<int> completion_tokens) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE food_parse_jobs SET status = 'done', result = $2::jsonb, error = NULL, model = $3, "
                " prompt_tokens = $4, completion_tokens = $5, finished_at = now() WHERE id = $1::uuid" +
                    JobRows::while_claimed(6),
                id,
                lines.dump(),
                model,
                prompt_tokens,
                completion_tokens,
                attempt);
            return true;
        });
    }

    /// @p error is "<code>: <message>"; the code is what the page reads.
    void fail(const std::string& id, const std::string& code, const std::string& message, int attempt = 0) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE food_parse_jobs SET status = 'failed', error = $2, finished_at = now() WHERE id = $1::uuid" +
                    JobRows::while_open(3),
                id,
                message.empty() ? code : code + ": " + message,
                attempt);
            return true;
        });
    }

private:
    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params(
            "SELECT row_to_json(t) FROM (SELECT id, " + JobRows::shown_status() +
                " AS status, text, meal, date::text, result, " + JobRows::shown_error() +
                " AS error, model, "
                " prompt_tokens, completion_tokens, "
                " " SQL_ISO_UTC("created_at") " AS created_at, "
                " " SQL_ISO_UTC("finished_at") " AS finished_at "
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
