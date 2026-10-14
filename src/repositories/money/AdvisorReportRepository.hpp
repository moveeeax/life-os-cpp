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
#include "repositories/JobRows.hpp"
#include "repositories/SqlTime.hpp"
#include "repositories/money/Errors.hpp"

namespace Repositories::Money {

class AdvisorReportRepository {
public:
    static constexpr const char* kTable = "money_advisor_reports";

    /**
     * The owner's review of the period, created queued when there is none.
     * A failed one is queued again (a "Run now" after a provider outage), and
     * so is a done one written before its period ended (finished on or before
     * period_end, UTC): the week has more rows now. So is a running one whose
     * worker died, or a queued one the queue lost (older than
     * JobRows::kStaleRunning either way: both read as failed).
     * @returns {row, true} when this call queued it (created or re-queued).
     */
    std::pair<nlohmann::json, bool> create_or_get(const std::string& owner,
                                                  const std::string& period,
                                                  const std::string& start,
                                                  const std::string& end) {
        return create_or_get(owner, period, start, end, [](auto&, const std::string&) {});
    }

    /// With @p on_queued(txn, id) run in the same transaction when this call
    /// queued the row: the outbox row of its queue job rides the commit
    /// (Jobs::Outbox::enqueue_if_drained), so a crash leaves no row without a job.
    template <typename OnQueued>
    std::pair<nlohmann::json, bool> create_or_get(const std::string& owner,
                                                  const std::string& period,
                                                  const std::string& start,
                                                  const std::string& end,
                                                  OnQueued&& on_queued) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) -> std::pair<nlohmann::json, bool> {
                    // A re-queue restarts the row's clock (started_at): the old one would read
                    // the row as lost again (JobRows::shown_status) before any worker saw it.
                    auto r = txn.exec_params(
                        "INSERT INTO money_advisor_reports (owner_id, period, period_start, period_end) "
                        "VALUES ($1::uuid, $2, $3::date, $4::date) "
                        "ON CONFLICT (owner_id, period, period_start) DO UPDATE SET status = 'queued', attempts = 0, "
                        " error = NULL, finished_at = NULL, started_at = now() "
                        "WHERE money_advisor_reports.status = 'failed' "
                        " OR (money_advisor_reports.status = 'done' AND money_advisor_reports.period_end >= "
                        "  (money_advisor_reports.finished_at AT TIME ZONE 'UTC')::date) "
                        // A review whose worker died, or one the queue lost, reads as failed
                        // (JobRows::shown_status): it can be asked again.
                        " OR (money_advisor_reports.status IN ('queued', 'running') AND "
                        "  COALESCE(money_advisor_reports.started_at, money_advisor_reports.created_at) < "
                        "  now() - $5::interval) "
                        "RETURNING id::text",
                        owner,
                        period,
                        start,
                        end,
                        JobRows::kStaleRunning);
                    if (!r.empty()) {
                        const std::string id = r[0][0].template as<std::string>();
                        on_queued(txn, id);
                        return {*find_in(txn, owner, id), true};
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
                " " +
                    JobRows::shown_status() + " AS status, " + JobRows::shown_error() +
                    " AS error, model, "
                    " " SQL_ISO_UTC("created_at") " AS created_at, "
                    " " SQL_ISO_UTC("finished_at") " AS finished_at "
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

    /// queued (or stale running) -> running, one more attempt; 0 when not claimable.
    int start(const std::string& id) { return JobRows::start(kTable, id); }

    /// The row's status now; nullopt when the row is gone.
    std::optional<std::string> status(const std::string& id) { return JobRows::status(kTable, id); }

    /// running -> queued, for an outage the queue will retry; only by the holder of @p attempt.
    void requeue(const std::string& id, int attempt) { JobRows::requeue(kTable, id, attempt); }

    /// Milliseconds until a running row may be claimed again; 0 when it is not running.
    long long stale_in_ms(const std::string& id) { return JobRows::stale_in_ms(kTable, id); }

    /// Only the holder of @p attempt (what start() returned) writes the result.
    void finish(const std::string& id,
                int attempt,
                const nlohmann::json& facts,
                const std::string& content,
                const std::string& model,
                std::optional<int> prompt_tokens,
                std::optional<int> completion_tokens) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE money_advisor_reports SET status = 'done', facts = $2::jsonb, content = $3, error = NULL, "
                " model = $4, prompt_tokens = $5, completion_tokens = $6, finished_at = now() WHERE id = $1::uuid" +
                    JobRows::while_claimed(7),
                id,
                facts.dump(),
                content,
                model,
                prompt_tokens,
                completion_tokens,
                attempt);
            return true;
        });
    }

    void fail(const std::string& id, const std::string& code, const std::string& message, int attempt = 0) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE money_advisor_reports SET status = 'failed', error = $2, finished_at = now() WHERE id = "
                "$1::uuid" +
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
            "SELECT row_to_json(t) FROM (SELECT id, period, to_char(period_start, 'YYYY-MM-DD') AS period_start, "
            " to_char(period_end, 'YYYY-MM-DD') AS period_end, " +
                JobRows::shown_status() + " AS status, facts, content, " + JobRows::shown_error() +
                " AS error, model, prompt_tokens, "
                " completion_tokens, "
                " " SQL_ISO_UTC("created_at") " AS created_at, "
                " " SQL_ISO_UTC("finished_at") " AS finished_at "
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
