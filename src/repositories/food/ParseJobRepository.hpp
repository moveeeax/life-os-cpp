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

class ParseJobRepository : public JobRows::JobTable<ParseJobRepository> {
public:
    static constexpr const char* kTable = "food_parse_jobs";

    /// A queued job; returns its row.
    nlohmann::json create(const std::string& owner,
                          const std::string& text,
                          const std::string& meal,
                          const std::string& date) {
        return create(owner, text, meal, date, [](auto&, const std::string&) {});
    }

    /// A queued job, with @p in_txn(txn, id) run in the same transaction right
    /// after the insert: the outbox row of its queue job rides the commit
    /// (Jobs::Outbox::enqueue_if_drained), so a crash leaves no row without a job.
    template <typename InTxn>
    nlohmann::json create(const std::string& owner,
                          const std::string& text,
                          const std::string& meal,
                          const std::string& date,
                          InTxn&& in_txn) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "INSERT INTO food_parse_jobs (owner_id, text, meal, date) VALUES ($1::uuid, $2, $3, $4::date) "
                "RETURNING id::text",
                owner,
                text,
                meal,
                date);
            const std::string id = r[0][0].template as<std::string>();
            in_txn(txn, id);
            return *find_in(txn, owner, id);
        });
    }

    /// A finished job of the unified parse (POST /parse): its food lines, read
    /// back like any food parse. Returns the id.
    template <typename Txn>
    static std::string insert_done_in(Txn& txn,
                                      const std::string& owner,
                                      const std::string& text,
                                      const std::string& meal,
                                      const std::string& date,
                                      const nlohmann::json& lines,
                                      const std::string& model) {
        auto r = txn.exec_params(
            "INSERT INTO food_parse_jobs (owner_id, text, meal, date, status, attempts, started_at, result, model, "
            " finished_at) VALUES ($1::uuid, $2, $3, $4::date, 'done', 1, now(), $5::jsonb, $6, now()) "
            "RETURNING id::text",
            owner,
            text,
            meal,
            date,
            lines.dump(),
            model);
        return r[0][0].template as<std::string>();
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

    /// Only the holder of @p attempt (what start() returned) writes the result.
    void finish(const std::string& id,
                int attempt,
                const nlohmann::json& lines,
                const std::string& model,
                std::optional<int> prompt_tokens,
                std::optional<int> completion_tokens) {
        JobRows::finish(kTable, id, attempt, lines, model, prompt_tokens, completion_tokens);
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
