/**
 * @file ParseJobRepository.hpp
 * @brief parse_jobs: one phrase of the unified parse (POST /api/v1/parse) on
 *        its way through the LLM, the rows of each kind it produced, and what
 *        the worker created when the job was asked to apply them.
 *
 * `result` is {food: [...], money: [...], tasks: [...], water: {ml} | null,
 * jobs: {food, money, tasks}, errors: {...}}: the lines of each kind in the
 * format of that module's parse, and the id of the finished child row the
 * worker filed in the module's job table, so the module's own accept route
 * takes the lines. `applied` is {food, money, tasks, water, skipped} or
 * {error} when applying failed and the lines wait for an accept by hand.
 */

#pragma once

#include <exception>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/JobRows.hpp"
#include "repositories/SqlTime.hpp"

namespace Repositories::UnifiedParse {

class ParseJobRepository {
public:
    static constexpr const char* kTable = "parse_jobs";

    /// What a parse may ask for besides the phrase: the worker applies the
    /// rows itself (a spend on @p account_id when it is an open account of the
    /// owner's), and the request's Idempotency-Key.
    struct Apply {
        bool apply = false;
        std::optional<std::string> account_id;
        std::optional<std::string> idempotency_key;
    };

    /// The claimed row as the worker's finish sees it.
    struct Row {
        std::string owner, text, meal, date;
        bool apply = false;
        std::optional<std::string> account_id;
    };

    nlohmann::json create(const std::string& owner,
                          const std::string& text,
                          const std::string& meal,
                          const std::string& date) {
        bool replayed = false;
        return create(owner, text, meal, date, Apply{}, replayed, [](auto&, const std::string&) {});
    }

    /// A queued job, with @p in_txn(txn, id) run in the same transaction right
    /// after the insert (the outbox row of its queue job rides the commit). A
    /// job the owner already created with the same Idempotency-Key is returned
    /// instead, with @p replayed set and @p in_txn not run.
    template <typename InTxn>
    nlohmann::json create(const std::string& owner,
                          const std::string& text,
                          const std::string& meal,
                          const std::string& date,
                          const Apply& a,
                          bool& replayed,
                          InTxn&& in_txn) {
        replayed = false;
        return Database::get().execute_write([&](auto& txn) {
            if (a.idempotency_key) {
                if (const auto id = JobRows::keyed_in(txn, kTable, owner, *a.idempotency_key)) {
                    replayed = true;
                    return *find_in(txn, owner, *id);
                }
            }
            // A concurrent request with the same key waits on the index and then finds its row.
            auto r = txn.exec_params(
                "INSERT INTO parse_jobs (owner_id, text, meal, date, auto_apply, account_id, idempotency_key) "
                "VALUES ($1::uuid, $2, $3, $4::date, $5, $6::uuid, $7) "
                "ON CONFLICT (owner_id, idempotency_key) WHERE idempotency_key IS NOT NULL DO NOTHING "
                "RETURNING id::text",
                owner,
                text,
                meal,
                date,
                a.apply,
                a.account_id,
                a.idempotency_key);
            if (r.empty()) {
                replayed = true;
                return *find_in(txn, owner, *JobRows::keyed_in(txn, kTable, owner, *a.idempotency_key));
            }
            const std::string id = r[0][0].template as<std::string>();
            in_txn(txn, id);
            return *find_in(txn, owner, id);
        });
    }

    /// The job as its owner sees it; nullopt for another owner's job.
    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

    /// What the worker needs; nullopt when the row is gone.
    std::optional<nlohmann::json> load_for_worker(const std::string& id) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT json_build_object('owner_id', owner_id, 'text', text, 'meal', meal, 'date', date::text, "
                " 'status', status) FROM parse_jobs WHERE id = $1::uuid",
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

    /**
     * Closes the claimed row as done and runs @p write(txn, row) in the same
     * transaction: it files the child rows (and, when row.apply, creates the
     * rows of every kind) and returns {result, applied}. Only the holder of
     * @p attempt closes the row, and a done row is never claimed again, so the
     * rows are applied once whatever redelivers the job. When the applying
     * write throws, the job is closed again without applying (row.apply
     * false), with `applied: {error}`: the lines wait for an accept by hand.
     */
    template <typename Write>
    void finish(const std::string& id,
                int attempt,
                const std::string& model,
                std::optional<int> prompt_tokens,
                std::optional<int> completion_tokens,
                Write&& write) {
        const auto run = [&](bool may_apply) {
            Database::get().execute_write([&](auto& txn) {
                auto r = txn.exec_params(
                    "UPDATE parse_jobs SET status = 'done', error = NULL, model = $2, prompt_tokens = $3, "
                    " completion_tokens = $4, finished_at = now() WHERE id = $1::uuid" +
                        JobRows::while_claimed(5) +
                        " RETURNING owner_id::text, text, meal, date::text, auto_apply, account_id::text",
                    id,
                    model,
                    prompt_tokens,
                    completion_tokens,
                    attempt);
                if (r.empty()) {
                    return true;
                }
                Row row;
                row.owner = r[0][0].template as<std::string>();
                row.text = r[0][1].template as<std::string>();
                row.meal = r[0][2].template as<std::string>();
                row.date = r[0][3].template as<std::string>();
                const bool asked = r[0][4].template as<bool>();
                row.apply = asked && may_apply;
                if (!r[0][5].is_null()) {
                    row.account_id = r[0][5].template as<std::string>();
                }
                nlohmann::json out = write(txn, row);
                if (asked && !may_apply) {
                    out["applied"] = nlohmann::json{{"error", "apply_failed"}};
                }
                txn.exec_params("UPDATE parse_jobs SET result = $2::jsonb, applied = $3::jsonb WHERE id = $1::uuid",
                                id,
                                out["result"].dump(),
                                out["applied"].is_null() ? std::optional<std::string>() : out["applied"].dump());
                return true;
            });
        };
        try {
            run(true);
        } catch (const std::exception&) {
            run(false);
        }
    }

    /// @p error is stored as "<code>: <message>".
    void fail(const std::string& id, const std::string& code, const std::string& message, int attempt = 0) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE parse_jobs SET status = 'failed', error = $2, finished_at = now() WHERE id = $1::uuid" +
                    JobRows::while_open(3),
                id,
                message.empty() ? code : code + ": " + message,
                attempt);
            return true;
        });
    }

    /// Jobs of the owner that are queued or running (a stale queued or running row is not counted).
    long open_count(const std::string& owner) { return JobRows::open_count(kTable, owner); }

private:
    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params(
            "SELECT row_to_json(t) FROM (SELECT id, " + JobRows::shown_status() +
                " AS status, text, meal, date::text AS date, auto_apply AS apply, account_id, result, applied, " +
                JobRows::shown_error() +
                " AS error, model, prompt_tokens, completion_tokens, "
                " " SQL_ISO_UTC("created_at") " AS created_at, "
                " " SQL_ISO_UTC("finished_at") " AS finished_at "
                " FROM parse_jobs WHERE owner_id = $1::uuid AND id = $2::uuid) t",
            owner,
            id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::UnifiedParse
