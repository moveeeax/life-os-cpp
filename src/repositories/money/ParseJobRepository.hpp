/**
 * @file ParseJobRepository.hpp
 * @brief money_parse_jobs: a text or a receipt photo on its way through the
 *        LLM, the lines it produced, and whether the user accepted them. The
 *        photo travels as base64 between the API and SQL (decode/encode in the
 *        query) and is cleared on every terminal state.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/money/Errors.hpp"

namespace Repositories::Money {

class ParseJobRepository {
public:
    /// A row `running` longer than this was left by a worker that died.
    static constexpr const char* kStaleRunning = "10 minutes";

    nlohmann::json create_text(const std::string& owner, const std::string& text, const std::string& hint_date) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO money_parse_jobs (owner_id, kind, text, hint_date) "
                        "VALUES ($1::uuid, 'text', $2, $3::date) RETURNING id::text",
                        owner,
                        text,
                        hint_date);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// @param image_base64 the bytes of the photo, base64 (no data: prefix).
    nlohmann::json create_receipt(const std::string& owner,
                                  const std::string& image_base64,
                                  const std::string& image_type,
                                  const std::string& hint_date) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO money_parse_jobs (owner_id, kind, image, image_type, hint_date) "
                        "VALUES ($1::uuid, 'receipt', decode($2, 'base64'), $3, $4::date) RETURNING id::text",
                        owner,
                        image_base64,
                        image_type,
                        hint_date);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// The job as its owner sees it (never the photo); nullopt for another owner's job.
    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

    /// What the worker needs, the photo as base64 included; nullopt when the row is gone.
    std::optional<nlohmann::json> load_for_worker(const std::string& id) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT json_build_object('owner_id', owner_id, 'kind', kind, 'text', text, "
                " 'image', translate(encode(image, 'base64'), E'\\n', ''), 'image_type', image_type, 'hint_date', "
                "hint_date::text, "
                " 'status', status) FROM money_parse_jobs WHERE id = $1::uuid",
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
                "UPDATE money_parse_jobs SET status = 'running', attempts = attempts + 1, started_at = now() "
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
            txn.exec_params("UPDATE money_parse_jobs SET status = 'queued' WHERE id = $1::uuid AND status = 'running'",
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
                "UPDATE money_parse_jobs SET status = 'done', result = $2::jsonb, error = NULL, model = $3, "
                " prompt_tokens = $4, completion_tokens = $5, image = NULL, finished_at = now() WHERE id = $1::uuid",
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
                "UPDATE money_parse_jobs SET status = 'failed', error = $2, image = NULL, finished_at = now() "
                "WHERE id = $1::uuid",
                id,
                message.empty() ? code : code + ": " + message);
            return true;
        });
    }

    /// Marks a done job accepted, once. False when it is not done or was accepted before.
    template <typename Txn>
    static bool mark_accepted_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params(
            "UPDATE money_parse_jobs SET accepted_at = now() WHERE owner_id = $1::uuid AND id = $2::uuid "
            " AND status = 'done' AND accepted_at IS NULL RETURNING id",
            owner,
            id);
        return !r.empty();
    }

    /// Jobs of the owner that are queued or running.
    long open_count(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT count(*) FROM money_parse_jobs WHERE owner_id = $1::uuid AND status IN ('queued', 'running')",
                owner);
            return r[0][0].template as<long>();
        });
    }

    /// Whether the photo is still stored (tests and the cleanup check).
    bool has_image(const std::string& id) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT image IS NOT NULL FROM money_parse_jobs WHERE id = $1::uuid", id);
            return !r.empty() && r[0][0].template as<bool>();
        });
    }

private:
    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params(
            "SELECT row_to_json(t) FROM (SELECT id, kind, status, text, hint_date::text AS hint_date, result, error, "
            " model, prompt_tokens, completion_tokens, "
            " to_char(accepted_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS accepted_at, "
            " to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS created_at, "
            " to_char(finished_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS finished_at "
            " FROM money_parse_jobs WHERE owner_id = $1::uuid AND id = $2::uuid) t",
            owner,
            id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Money
