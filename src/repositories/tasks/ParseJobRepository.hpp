/**
 * @file ParseJobRepository.hpp
 * @brief task_parse_jobs: one phrase on its way through the LLM, the draft
 *        lines it produced, and whether the user accepted them. A phrase may
 *        come from an inbox note; accepting archives that note. A job created
 *        with apply is turned into tasks by the worker itself (`applied`).
 */

#pragma once

#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/JobRows.hpp"
#include "repositories/SqlTime.hpp"
#include "repositories/tasks/Errors.hpp"
#include "repositories/tasks/InboxRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"

namespace Repositories::Tasks {

class ParseJobRepository {
public:
    static constexpr const char* kTable = "task_parse_jobs";

    /// @throws NotFound when @p note_id is not the owner's note.
    nlohmann::json create(const std::string& owner,
                          const std::string& text,
                          const std::string& hint_date,
                          const std::optional<std::string>& note_id) {
        return create(owner, text, hint_date, note_id, [](auto&, const std::string&) {});
    }

    /// With @p in_txn(txn, id) run in the same transaction right after the
    /// insert: the outbox row of its queue job rides the commit
    /// (Jobs::Outbox::enqueue_if_drained), so a crash leaves no row without a job.
    template <typename InTxn>
    nlohmann::json create(const std::string& owner,
                          const std::string& text,
                          const std::string& hint_date,
                          const std::optional<std::string>& note_id,
                          InTxn&& in_txn) {
        bool replayed = false;
        return create(owner, text, hint_date, note_id, Apply{}, replayed, std::forward<InTxn>(in_txn));
    }

    /// What a parse may ask for besides the phrase: the worker creates the
    /// tasks itself when the job ends, and the request's Idempotency-Key.
    struct Apply {
        bool apply = false;
        std::optional<std::string> idempotency_key;
    };

    /// As above; a job the owner already created with the same Idempotency-Key
    /// is returned instead, with @p replayed set and @p in_txn not run.
    template <typename InTxn>
    nlohmann::json create(const std::string& owner,
                          const std::string& text,
                          const std::string& hint_date,
                          const std::optional<std::string>& note_id,
                          const Apply& a,
                          bool& replayed,
                          InTxn&& in_txn) {
        replayed = false;
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    if (a.idempotency_key) {
                        if (const auto id = JobRows::keyed_in(txn, kTable, owner, *a.idempotency_key)) {
                            replayed = true;
                            return *find_in(txn, owner, *id);
                        }
                    }
                    if (note_id.has_value() && !InboxRepository::exists_in(txn, owner, *note_id)) {
                        throw NotFound("task_note");
                    }
                    // A concurrent request with the same key waits on the index and then finds its row.
                    auto r = txn.exec_params(
                        "INSERT INTO task_parse_jobs (owner_id, text, hint_date, note_id, auto_apply, idempotency_key) "
                        "VALUES ($1::uuid, $2, $3::date, $4::uuid, $5, $6) "
                        "ON CONFLICT (owner_id, idempotency_key) WHERE idempotency_key IS NOT NULL DO NOTHING "
                        "RETURNING id::text",
                        owner,
                        text,
                        hint_date,
                        note_id,
                        a.apply,
                        a.idempotency_key);
                    if (r.empty()) {
                        replayed = true;
                        return *find_in(txn, owner, *JobRows::keyed_in(txn, kTable, owner, *a.idempotency_key));
                    }
                    const std::string id = r[0][0].template as<std::string>();
                    in_txn(txn, id);
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// A finished job of the unified parse (POST /parse): its draft lines, for
    /// the accept route or, with @p apply, already created. Returns the id.
    template <typename Txn>
    static std::string insert_done_in(Txn& txn,
                                      const std::string& owner,
                                      const std::string& text,
                                      const std::string& hint_date,
                                      const nlohmann::json& lines,
                                      const std::string& model,
                                      bool apply) {
        auto r = txn.exec_params(
            "INSERT INTO task_parse_jobs (owner_id, text, hint_date, status, attempts, started_at, result, model, "
            " auto_apply, finished_at) VALUES ($1::uuid, $2, $3::date, 'done', 1, now(), $4::jsonb, $5, $6, now()) "
            "RETURNING id::text",
            owner,
            text,
            hint_date,
            lines.dump(),
            model,
            apply);
        return r[0][0].template as<std::string>();
    }

    /// What the worker created for an applied job; the job reads as accepted from now on.
    template <typename Txn>
    static void mark_applied_in(Txn& txn, const std::string& id, const nlohmann::json& applied) {
        txn.exec_params("UPDATE task_parse_jobs SET applied = $2::jsonb, accepted_at = now() WHERE id = $1::uuid",
                        id,
                        applied.dump());
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
                const nlohmann::json& lines,
                const std::string& model,
                std::optional<int> prompt_tokens,
                std::optional<int> completion_tokens) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE task_parse_jobs SET status = 'done', result = $2::jsonb, error = NULL, model = $3, "
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

    /// finish() and, for a job created with apply, @p apply(txn, owner) in the
    /// same transaction, with the job's inbox note archived and linked to the
    /// first task as an accept does. The claim fences the close, so the tasks
    /// are created once whatever redelivers the job; an apply that throws
    /// leaves the job done with `applied: {error}` and the lines to accept by hand.
    template <typename ApplyFn>
    void finish_applying(const std::string& id,
                         int attempt,
                         const nlohmann::json& lines,
                         const std::string& model,
                         std::optional<int> prompt_tokens,
                         std::optional<int> completion_tokens,
                         ApplyFn&& apply) {
        const auto close = [&](auto& txn) {
            return txn.exec_params(
                "UPDATE task_parse_jobs SET status = 'done', result = $2::jsonb, error = NULL, model = $3, "
                " prompt_tokens = $4, completion_tokens = $5, finished_at = now() WHERE id = $1::uuid" +
                    JobRows::while_claimed(6) + " RETURNING owner_id::text, auto_apply",
                id,
                lines.dump(),
                model,
                prompt_tokens,
                completion_tokens,
                attempt);
        };
        try {
            Database::get().execute_write([&](auto& txn) {
                auto r = close(txn);
                if (r.empty() || !r[0][1].template as<bool>()) {
                    return true;
                }
                const std::string owner = r[0][0].template as<std::string>();
                const nlohmann::json created = apply(txn, owner);
                if (const auto note = note_in(txn, owner, id); note.has_value() && !created.empty()) {
                    InboxRepository::archive_in(txn, owner, *note, created[0]["id"].get<std::string>());
                }
                mark_applied_in(txn, id, nlohmann::json{{"tasks", created}, {"skipped", nlohmann::json::array()}});
                return true;
            });
        } catch (const std::exception&) {
            Database::get().execute_write([&](auto& txn) {
                auto r = close(txn);
                if (!r.empty() && r[0][1].template as<bool>()) {
                    txn.exec_params("UPDATE task_parse_jobs SET applied = $2::jsonb WHERE id = $1::uuid",
                                    id,
                                    nlohmann::json{{"error", "apply_failed"}}.dump());
                }
                return true;
            });
        }
    }

    /// @p error is stored as "<code>: <message>".
    void fail(const std::string& id, const std::string& code, const std::string& message, int attempt = 0) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE task_parse_jobs SET status = 'failed', error = $2, finished_at = now() WHERE id = $1::uuid" +
                    JobRows::while_open(3),
                id,
                message.empty() ? code : code + ": " + message,
                attempt);
            return true;
        });
    }

    /// The edited lines as tasks, the note (if any) archived and linked to the first one, the job
    /// marked accepted: one transaction. @throws AlreadyAccepted when the job is not done or was
    /// accepted before (another owner's job reads the same way), Invariant.
    nlohmann::json accept(const std::string& owner,
                          const std::string& id,
                          const std::vector<TaskRepository::Input>& inputs) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto marked = txn.exec_params(
                        "UPDATE task_parse_jobs SET accepted_at = now() WHERE owner_id = $1::uuid AND id = $2::uuid "
                        " AND status = 'done' AND accepted_at IS NULL RETURNING id",
                        owner,
                        id);
                    if (marked.empty()) {
                        throw AlreadyAccepted();
                    }
                    nlohmann::json out = nlohmann::json::array();
                    for (const auto& in : inputs) {
                        out.push_back(*TaskRepository::find_in(txn, owner, TaskRepository::create_in(txn, owner, in)));
                    }
                    if (const auto note = note_in(txn, owner, id); note.has_value() && !out.empty()) {
                        InboxRepository::archive_in(txn, owner, *note, out[0]["id"].get<std::string>());
                    }
                    return out;
                });
            },
            &detail::translate);
    }

    /// Jobs of the owner that are queued or running (a stale queued or running row is not counted).
    long open_count(const std::string& owner) { return JobRows::open_count(kTable, owner); }

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
            "SELECT row_to_json(t) FROM (SELECT id, " + JobRows::shown_status() +
                " AS status, text, hint_date::text AS hint_date, note_id, result, " + JobRows::shown_error() +
                " AS"
                " error, model, prompt_tokens, completion_tokens, auto_apply AS apply, applied, "
                " " SQL_ISO_UTC("accepted_at") " AS accepted_at, "
                " " SQL_ISO_UTC("created_at") " AS created_at, "
                " " SQL_ISO_UTC("finished_at") " AS finished_at "
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
