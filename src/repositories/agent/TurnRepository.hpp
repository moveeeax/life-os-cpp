/**
 * @file TurnRepository.hpp
 * @brief ani_turns (migration 059): the user's messages to Ani's agent and its
 *        replies, as the chat panel shows them. A turn is queued by the API,
 *        run by the worker (agent/Turn.hpp) and ends done or failed; a user
 *        has at most one turn open at a time (a unique partial index).
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories::Agent {

class TurnRepository {
public:
    /// An open turn older than this is taken for lost and failed as a timeout.
    static constexpr int kStaleSeconds = 900;

    /// Queues a turn. nullopt while another turn of the user is open; an open
    /// turn older than kStaleSeconds is failed first, so a lost job cannot
    /// lock the chat.
    std::optional<nlohmann::json> open(const std::string& owner, const std::string& text, const std::string& tz) {
        return Database::get().execute_write([&](auto& txn) -> std::optional<nlohmann::json> {
            txn.exec_params(
                "UPDATE ani_turns SET status = 'failed', error = 'timeout' WHERE owner_id = $1::uuid "
                "AND status IN ('queued', 'running') AND created_at < now() - make_interval(secs => $2)",
                owner,
                kStaleSeconds);
            auto r = txn.exec_params(
                "INSERT INTO ani_turns (owner_id, text, tz) VALUES ($1::uuid, $2, $3) "
                "ON CONFLICT (owner_id) WHERE status IN ('queued', 'running') DO NOTHING RETURNING id::text",
                owner,
                text,
                tz);
            if (r.empty()) {
                return std::nullopt;
            }
            return one_(txn, r[0][0].template as<std::string>(), owner);
        });
    }

    /// The user's turns created in the last @p seconds: the rate limit.
    int count_since(const std::string& owner, int seconds) {
        return Database::get().execute_read([&](auto& txn) {
            return txn
                .exec_params(
                    "SELECT count(*) FROM ani_turns WHERE owner_id = $1::uuid AND created_at > now() - "
                    "make_interval(secs => $2)",
                    owner,
                    seconds)[0][0]
                .template as<int>();
        });
    }

    /// One turn of the user, as the panel shows it.
    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read(
            [&](auto& txn) -> std::optional<nlohmann::json> { return one_(txn, id, owner); });
    }

    /// The user's turns, newest first; @p before (a turn id) pages back from that turn.
    nlohmann::json list(const std::string& owner, const std::string& before, int limit) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                    " FROM ani_turns WHERE owner_id = $1::uuid AND ($2 = '' OR created_at < (SELECT created_at "
                    "FROM ani_turns WHERE id::text = $2 AND owner_id = $1::uuid)) ORDER BY created_at DESC LIMIT $3) t",
                owner,
                before,
                limit);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// What the worker needs to run a turn: the view plus owner_id, tz and its
    /// age in seconds. Any user's turn: the job carries only the id.
    std::optional<nlohmann::json> for_worker(const std::string& id) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                    ", owner_id::text AS owner_id, tz, EXTRACT(EPOCH FROM now() - created_at)::bigint AS "
                    "age_seconds FROM ani_turns WHERE id::text = $1) t",
                id);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// queued -> running. False when the turn is not queued (run already, or ended).
    bool start(const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            return !txn.exec_params(
                           "UPDATE ani_turns SET status = 'running' WHERE id::text = $1 AND status = 'queued' "
                           "RETURNING id",
                           id)
                        .empty();
        });
    }

    /// running -> queued: the agent refused the turn before it reached the model, so it is sent again.
    bool requeue(const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            return !txn.exec_params(
                           "UPDATE ani_turns SET status = 'queued' WHERE id::text = $1 AND status = 'running' "
                           "RETURNING id",
                           id)
                        .empty();
        });
    }

    /// running -> done with the reply and the tools it ran.
    bool finish(const std::string& id, const std::string& reply, const nlohmann::json& tools) {
        return Database::get().execute_write([&](auto& txn) {
            return !txn.exec_params(
                           "UPDATE ani_turns SET status = 'done', reply = $2, tools = $3::jsonb WHERE id::text = $1 "
                           "AND status = 'running' RETURNING id",
                           id,
                           reply,
                           tools.dump())
                        .empty();
        });
    }

    /// queued|running -> failed with a short code (see migration 059).
    bool fail(const std::string& id, const std::string& error) {
        return Database::get().execute_write([&](auto& txn) {
            return !txn.exec_params(
                           "UPDATE ani_turns SET status = 'failed', error = $2 WHERE id::text = $1 AND status IN "
                           "('queued', 'running') RETURNING id",
                           id,
                           error)
                        .empty();
        });
    }

    /// The wall clock in @p tz as the agent reads it ("2026-10-25 19:41, Sun"), or
    /// nullopt for a zone Postgres does not know.
    std::optional<std::string> local_now(const std::string& tz) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<std::string> {
            auto r = txn.exec_params(
                "SELECT to_char(now() AT TIME ZONE name, 'YYYY-MM-DD HH24:MI, Dy') FROM pg_timezone_names WHERE "
                "name = $1",
                tz);
            if (r.empty()) {
                return std::nullopt;
            }
            return r[0][0].template as<std::string>();
        });
    }

private:
    static constexpr const char* kColumns =
        "id::text AS id, status, text, reply, error, tools, "
        "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at, "
        "to_char(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS updated_at";

    template <typename Txn>
    static std::optional<nlohmann::json> one_(Txn& txn, const std::string& id, const std::string& owner) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM ani_turns WHERE id::text = $1 AND owner_id = $2::uuid) t",
                                 id,
                                 owner);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Agent
