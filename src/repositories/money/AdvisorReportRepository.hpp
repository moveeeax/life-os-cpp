/**
 * @file AdvisorReportRepository.hpp
 * @brief money_advisor_reports: a review per owner and period, its facts, its
 *        text, and the job life cycle shared with the parse jobs (attempts,
 *        a stale `running` row claimed again).
 */

#pragma once

#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/money/Errors.hpp"

namespace Repositories::Money {

class AdvisorReportRepository {
public:
    static constexpr const char* kStaleRunning = "10 minutes";

    /**
     * The owner's review of the period, created queued when there is none.
     * A failed one is queued again (a "Run now" after a provider outage).
     * @returns {row, true} when this call queued it (created or re-queued).
     */
    std::pair<nlohmann::json, bool> create_or_get(const std::string& owner,
                                                  const std::string& period,
                                                  const std::string& start,
                                                  const std::string& end) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) -> std::pair<nlohmann::json, bool> {
                    auto r = txn.exec_params(
                        "INSERT INTO money_advisor_reports (owner_id, period, period_start, period_end) "
                        "VALUES ($1::uuid, $2, $3::date, $4::date) "
                        "ON CONFLICT (owner_id, period, period_start) DO UPDATE SET status = 'queued', attempts = 0, "
                        " error = NULL, finished_at = NULL WHERE money_advisor_reports.status = 'failed' "
                        "RETURNING id::text",
                        owner,
                        period,
                        start,
                        end);
                    if (!r.empty()) {
                        return {*find_in(txn, owner, r[0][0].template as<std::string>()), true};
                    }
                    auto existing = txn.exec_params(
                        "SELECT id::text FROM money_advisor_reports WHERE owner_id = $1::uuid AND period = $2 "
                        " AND period_start = $3::date",
                        owner,
                        period,
                        start);
                    return {*find_in(txn, owner, existing[0][0].template as<std::string>()), false};
                });
            },
            &detail::translate);
    }

    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

    /// The owner's reviews, newest period first, without their facts (the list stays small).
    nlohmann::json list(const std::string& owner, long limit) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT id, period, "
                " to_char(period_start, 'YYYY-MM-DD') AS period_start, to_char(period_end, 'YYYY-MM-DD') AS "
                "period_end, "
                " status, error, model, "
                " to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS created_at, "
                " to_char(finished_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS finished_at "
                " FROM money_advisor_reports WHERE owner_id = $1::uuid ORDER BY period_start DESC, created_at DESC "
                " LIMIT $2) t",
                owner,
                limit);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// What the worker needs; nullopt when the row is gone.
    std::optional<nlohmann::json> load_for_worker(const std::string& id) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT json_build_object('owner_id', owner_id, 'period', period, "
                " 'period_start', period_start::text, 'period_end', period_end::text, 'status', status) "
                "FROM money_advisor_reports WHERE id = $1::uuid",
                id);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    int start(const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "UPDATE money_advisor_reports SET status = 'running', attempts = attempts + 1, started_at = now() "
                "WHERE id = $1::uuid AND (status = 'queued' OR (status = 'running' AND "
                " COALESCE(started_at, created_at) < now() - $2::interval)) RETURNING attempts",
                id,
                kStaleRunning);
            return r.empty() ? 0 : r[0][0].template as<int>();
        });
    }

    void requeue(const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE money_advisor_reports SET status = 'queued' WHERE id = $1::uuid AND status = 'running'", id);
            return true;
        });
    }

    void finish(const std::string& id,
                const nlohmann::json& facts,
                const std::string& content,
                const std::string& model,
                std::optional<int> prompt_tokens,
                std::optional<int> completion_tokens) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE money_advisor_reports SET status = 'done', facts = $2::jsonb, content = $3, error = NULL, "
                " model = $4, prompt_tokens = $5, completion_tokens = $6, finished_at = now() WHERE id = $1::uuid",
                id,
                facts.dump(),
                content,
                model,
                prompt_tokens,
                completion_tokens);
            return true;
        });
    }

    void fail(const std::string& id, const std::string& code, const std::string& message) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE money_advisor_reports SET status = 'failed', error = $2, finished_at = now() WHERE id = "
                "$1::uuid",
                id,
                message.empty() ? code : code + ": " + message);
            return true;
        });
    }

private:
    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params(
            "SELECT row_to_json(t) FROM (SELECT id, period, to_char(period_start, 'YYYY-MM-DD') AS period_start, "
            " to_char(period_end, 'YYYY-MM-DD') AS period_end, status, facts, content, error, model, prompt_tokens, "
            " completion_tokens, "
            " to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS created_at, "
            " to_char(finished_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS finished_at "
            " FROM money_advisor_reports WHERE owner_id = $1::uuid AND id = $2::uuid) t",
            owner,
            id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Money
