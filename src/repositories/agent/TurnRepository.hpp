/**
 * @file TurnRepository.hpp
 * @brief ani_turns (migration 059): the user's messages to Ani's agent and its
 *        replies, as the chat panel shows them. A turn is queued by the API,
 *        run by the worker (agent/Turn.hpp) and ends done or failed. A turn
 *        is the user's or the webhook's (origin, migration 062); each origin
 *        has at most one turn open (unique partial indexes), and the worker
 *        runs one at a time. A turn's view carries the actions it proposed
 *        (ActionRepository).
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/agent/ActionRepository.hpp"

namespace Repositories::Agent {

class TurnRepository {
public:
    /// An open turn older than this is taken for lost and failed as a timeout.
    static constexpr int kStaleSeconds = 900;

    /// Queues a turn of @p origin ("user" or "hook"). nullopt while another
    /// turn of the user from the same origin is open; an open turn older than
    /// kStaleSeconds is failed first, so a lost job cannot lock the chat.
    std::optional<nlohmann::json> open(const std::string& owner,
                                       const std::string& text,
                                       const std::string& tz,
                                       const std::string& origin = "user") {
        return Database::get().execute_write([&](auto& txn) -> std::optional<nlohmann::json> {
            txn.exec_params(
                "UPDATE ani_turns SET status = 'failed', error = 'timeout' WHERE owner_id = $1::uuid "
                "AND status IN ('queued', 'running') AND origin = $3 AND created_at < now() - make_interval(secs => "
                "$2)",
                owner,
                kStaleSeconds,
                origin);
            // The conflict target names the origin's own index (exactly its predicate).
            const std::string target = origin == "hook" ? "'hook'" : "'user'";
            auto r = txn.exec_params("INSERT INTO ani_turns (owner_id, text, tz, origin) VALUES ($1::uuid, $2, $3, " +
                                         target +
                                         ") ON CONFLICT (owner_id) WHERE status IN ('queued', 'running') AND "
                                         "origin = " +
                                         target + " DO NOTHING RETURNING id::text",
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
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + columns() +
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
                "SELECT row_to_json(t) FROM (SELECT " + columns() +
                    ", owner_id::text AS owner_id, tz, EXTRACT(EPOCH FROM now() - created_at)::bigint AS "
                    "age_seconds FROM ani_turns WHERE id::text = $1) t",
                id);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// queued -> running. False when the turn is not queued (run already, or
    /// ended), or while another turn of the user runs: a user runs one turn at
    /// a time (a per-user lock makes the check and the change one step).
    bool start(const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            auto o = txn.exec_params("SELECT owner_id::text FROM ani_turns WHERE id::text = $1", id);
            if (o.empty()) {
                return false;
            }
            const std::string owner = o[0][0].template as<std::string>();
            txn.exec_params("SELECT pg_advisory_xact_lock(hashtext('ani_turn_run:' || $1))", owner);
            return !txn.exec_params(
                           "UPDATE ani_turns SET status = 'running' WHERE id::text = $1 AND status = 'queued' AND "
                           "NOT EXISTS (SELECT 1 FROM ani_turns r WHERE r.owner_id = $2::uuid AND r.status = "
                           "'running' AND r.id::text <> $1) RETURNING id",
                           id,
                           owner)
                        .empty();
        });
    }

    /// The zone of the user's last turn they wrote (for a webhook turn); UTC without one.
    std::string last_tz(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT tz FROM ani_turns WHERE owner_id = $1::uuid AND origin = 'user' ORDER BY created_at DESC "
                "LIMIT 1",
                owner);
            return r.empty() ? std::string("UTC") : r[0][0].template as<std::string>();
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

    enum class Removed { Done, Open, Missing };

    /// Deletes one turn of the user; one still queued or running stays (Open).
    Removed remove(const std::string& owner, const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "WITH t AS (SELECT status FROM ani_turns WHERE id::text = $1 AND owner_id = $2::uuid), "
                "d AS (DELETE FROM ani_turns WHERE id::text = $1 AND owner_id = $2::uuid "
                "AND status IN ('done', 'failed') RETURNING id) "
                "SELECT (SELECT status FROM t), (SELECT count(*) FROM d)",
                id,
                owner);
            if (r[0][0].is_null()) {
                return Removed::Missing;
            }
            return r[0][1].template as<int>() > 0 ? Removed::Done : Removed::Open;
        });
    }

    /// Whether a turn of the user is queued or running.
    bool has_open(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            return !txn.exec_params(
                           "SELECT 1 FROM ani_turns WHERE owner_id = $1::uuid AND status IN ('queued', 'running')",
                           owner)
                        .empty();
        });
    }

    /// Fails the user's open turns older than kStaleSeconds (their job was
    /// lost or their worker died) and returns their ids, so nothing waits on
    /// them for ever.
    std::vector<std::string> reap_stale(const std::string& owner) {
        return Database::get().execute_write([&](auto& txn) {
            std::vector<std::string> ids;
            for (const auto& row : txn.exec_params(
                     "UPDATE ani_turns SET status = 'failed', error = 'timeout' WHERE owner_id = $1::uuid AND status "
                     "IN ('queued', 'running') AND created_at < now() - make_interval(secs => $2) RETURNING id::text",
                     owner,
                     kStaleSeconds)) {
                ids.push_back(row[0].template as<std::string>());
            }
            return ids;
        });
    }

    /// Whether a turn of the user from another origin than @p origin is
    /// running: the worker runs one turn of a user at a time.
    bool running_other(const std::string& owner, const std::string& origin) {
        return Database::get().execute_read([&](auto& txn) {
            return !txn.exec_params(
                           "SELECT 1 FROM ani_turns WHERE owner_id = $1::uuid AND status = 'running' AND "
                           "origin <> $2",
                           owner,
                           origin)
                        .empty();
        });
    }

    /// The user's open webhook turn (queued or running), with its owner_id.
    std::optional<nlohmann::json> open_hook_turn(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT row_to_json(t) FROM (SELECT id::text AS id, status FROM ani_turns WHERE owner_id = $1::uuid "
                "AND origin = 'hook' AND status IN ('queued', 'running')) t",
                owner);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// Adds a line to a turn still queued (events that came while it waited).
    /// False when it runs already or the text would pass the column's limit.
    bool append_text(const std::string& id, const std::string& text) {
        return Database::get().execute_write([&](auto& txn) {
            return !txn.exec_params(
                           "UPDATE ani_turns SET text = text || E'\\n' || $2 WHERE id::text = $1 AND status = "
                           "'queued' AND char_length(text) + 1 + char_length($2) <= 4000 RETURNING id",
                           id,
                           text)
                        .empty();
        });
    }

    /// Deletes every ended turn of the user; returns how many went.
    int clear(const std::string& owner) {
        return Database::get().execute_write([&](auto& txn) {
            return static_cast<int>(txn.exec_params("DELETE FROM ani_turns WHERE owner_id = $1::uuid AND status IN "
                                                    "('done', 'failed') RETURNING id",
                                                    owner)
                                        .size());
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
    /// The turn as the panel shows it, with the actions it proposed.
    static std::string columns() {
        return "id::text AS id, status, origin, text, reply, error, tools, "
               "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at, "
               "to_char(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS updated_at, " +
               ActionRepository::of_turn_sql();
    }

    template <typename Txn>
    static std::optional<nlohmann::json> one_(Txn& txn, const std::string& id, const std::string& owner) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + columns() +
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
