/**
 * @file ParseJobStart.hpp
 * @brief The start of an LLM parse job, shared by the parse routes of Money,
 *        Tasks, Food and the unified parse: the replay of a request already
 *        taken, the open-job cap, the row and its queue job in one
 *        transaction, and the fail of a row the queue did not take.
 *
 * The handler validates its body, checks that its parse is set up, and hands
 * over a create callback; this file answers 202 with {data: {id, status}} or
 * one of 429 too_many_parses, 503 storage_unavailable, 503 queue_unavailable.
 */

#pragma once

#include <exception>
#include <functional>
#include <optional>
#include <pqxx/pqxx>
#include <stdexcept>
#include <string>

#include <drogon/HttpResponse.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/HandlerSupport.hpp"
#include "jobs/Jobs.hpp"
#include "jobs/Outbox.hpp"
#include "repositories/JobRows.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

/// Which job a parse route starts.
struct ParseJobSpec {
    const char* table;     ///< the job table (the repository's kTable)
    const char* job_type;  ///< the queue's job type
    const char* label;     ///< the log label ("money parse")
    const char* keyed_op;  ///< the with_repo_errors label of the replay lookup ("money.parse.keyed")
};

/// No module mapping for a create error: every one answers storage_unavailable.
inline drogon::HttpResponsePtr no_create_errors(const std::exception&) {
    return nullptr;
}

/**
 * Start a parse job of @p spec for @p owner.
 *
 * @param key    The request's Idempotency-Key: a job the owner created with it
 *               answers again, before the cap. nullopt for a route without replay.
 * @param create `create(repo, replayed, in_txn) -> json`: inserts the row and
 *               runs `in_txn(txn, id)` in the same transaction; it sets
 *               @p replayed (and skips in_txn) when it returns the job an
 *               earlier request with the same key created.
 * @param map_error The module's answer to an error of the create (a note that
 *               is not the owner's, a bad value at the SQL boundary), or
 *               nullptr for storage_unavailable. The log line never carries the
 *               error's text: the server's DETAIL would quote the user's text.
 */
template <typename Repo, typename Create, typename MapError>
void start_parse_job(const std::string& owner,
                     const std::function<void(const drogon::HttpResponsePtr&)>& callback,
                     const std::optional<std::string>& key,
                     const ParseJobSpec& spec,
                     Create&& create,
                     MapError&& map_error) {
    // A retry of a request already taken answers with its job, before the cap.
    if (key) {
        std::optional<nlohmann::json> earlier;
        if (!with_repo_errors(
                callback, spec.keyed_op, [&] { earlier = Repositories::JobRows::keyed(spec.table, owner, *key); })) {
            return;
        }
        if (earlier) {
            callback(job_accepted(*earlier));
            return;
        }
    }
    Repo repo;
    std::string id;
    bool replayed = false;
    // The job carries the retry limit it was submitted with.
    const int max_attempts = Jobs::is_initialized() ? Jobs::get().default_max_retries() : Jobs::kDefaultMaxRetries;
    const auto payload = [&](const std::string& job_id) {
        return nlohmann::json{{"job_id", job_id}, {"owner_id", owner}, {"max_attempts", max_attempts}};
    };
    // The job rides the row's transaction as an outbox row when a drain is configured
    // (Jobs::Outbox::enqueue_if_drained): a crash after the commit cannot leave the row
    // queued with no job behind it. With the drain off (OUTBOX_DRAIN_INTERVAL_SEC=0: the
    // compose stack, the test suites) the direct submit below follows the commit, as before.
    bool via_outbox = false;
    try {
        // Each parse costs provider tokens: a few in flight per user is plenty.
        if (repo.open_count(owner) >= Repositories::JobRows::kOpenMax) {
            callback(too_many_parses());
            return;
        }
        const nlohmann::json job = create(repo, replayed, [&](auto& txn, const std::string& job_id) {
            // The cap again, under the lock: two creates at once cannot both pass the count above.
            Repositories::JobRows::hold_slot(txn, spec.table, owner);
            via_outbox = Jobs::Outbox::enqueue_if_drained(txn, spec.job_type, payload(job_id));
        });
        if (replayed) {
            callback(job_accepted(job));
            return;
        }
        id = job["id"].template get<std::string>();
    } catch (const Repositories::JobRows::SlotsTaken&) {
        callback(too_many_parses());
        return;
    } catch (const std::exception& e) {
        if (const auto mapped = map_error(e)) {
            callback(mapped);
            return;
        }
        // The server's DETAIL would quote the row (the text, the photo): the state only.
        if (const auto* sql = dynamic_cast<const pqxx::sql_error*>(&e)) {
            spdlog::warn("{} create failed: SQLSTATE {}", spec.label, sql->sqlstate());
        } else {
            spdlog::warn("{} create failed", spec.label);
        }
        callback(ErrorResponse::service_unavailable("storage_unavailable"));
        return;
    }
    try {
        if (!via_outbox) {
            if (!Jobs::is_initialized()) {
                throw std::runtime_error("the job queue is not initialized");
            }
            Jobs::get().submit(spec.job_type, payload(id));
        }
    } catch (const std::exception& e) {
        spdlog::warn("{} enqueue unavailable: {}", spec.label, e.what());
        // Nothing will ever run it: the row must not look queued to the page.
        try {
            repo.fail(id, "queue_unavailable", "the job queue did not take the job");
        } catch (const std::exception& inner) {
            spdlog::warn("{} {}: could not mark failed: {}", spec.label, id, inner.what());
        }
        callback(ErrorResponse::service_unavailable("queue_unavailable"));
        return;
    }
    callback(job_accepted(nlohmann::json{{"id", id}, {"status", "queued"}}));
}

}  // namespace Api
