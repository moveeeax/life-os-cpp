/**
 * @file AgentRepository.hpp
 * @brief ani_agents and ani_usage (migration 058): the state of each user's
 *        agent pod and the tokens its model spent per month. Every state
 *        change is one conditional UPDATE, so two jobs or a job and a request
 *        racing on the same row cannot both win a transition.
 *
 *        stopped -> starting   (a wake; the API)
 *        starting -> running   (the pod is ready; the worker)
 *        starting|running -> stopping   (logout, idle, too old, a failure)
 *        stopping -> stopped   (the pod is gone and its snapshot checked)
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories::Agent {

class AgentRepository {
public:
    /// The row with its ages in seconds, or nullopt for a user who never woke the agent.
    std::optional<nlohmann::json> get(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                         " FROM ani_agents WHERE owner_id = $1::uuid) t",
                                     owner);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// stopped (or no row) -> starting. False when the agent is already up or on its way.
    bool begin_start(const std::string& owner) {
        return Database::get().execute_write([&](auto& txn) {
            return !txn.exec_params(
                           "INSERT INTO ani_agents (owner_id, state, last_active_at) VALUES ($1::uuid, 'starting', "
                           "now()) "
                           "ON CONFLICT (owner_id) DO UPDATE SET state = 'starting', last_active_at = now(), "
                           "last_error = NULL WHERE ani_agents.state = 'stopped' RETURNING owner_id",
                           owner)
                        .empty();
        });
    }

    /// Agents not stopped, across users: what counts against the pod limit.
    int count_active() {
        return Database::get().execute_read([](auto& txn) {
            return txn.exec("SELECT count(*) FROM ani_agents WHERE state <> 'stopped'")[0][0].template as<int>();
        });
    }

    /// The user is around: pushes the idle stop back. A stopped agent stays stopped.
    void touch(const std::string& owner) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE ani_agents SET last_active_at = now() WHERE owner_id = $1::uuid AND state IN ('starting', "
                "'running')",
                owner);
            return true;
        });
    }

    /// The tokens of a run being started and the folder of its snapshots; only while the row is starting.
    bool set_run(const std::string& owner,
                 const std::string& pod_name,
                 const std::string& gateway_token,
                 const std::string& proxy_token_hash,
                 const std::string& snapshot_dir) {
        return Database::get().execute_write([&](auto& txn) {
            return !txn.exec_params(
                           "UPDATE ani_agents SET pod_name = $2, gateway_token = $3, proxy_token_hash = $4, "
                           "snapshot_dir = $5, started_at = now(), pod_ip = NULL WHERE owner_id = $1::uuid AND state "
                           "= 'starting' RETURNING owner_id",
                           owner,
                           pod_name,
                           gateway_token,
                           proxy_token_hash,
                           snapshot_dir)
                        .empty();
        });
    }

    /// starting -> running, with the pod's address; only a run whose pod was created (set_run).
    bool set_running(const std::string& owner, const std::string& pod_ip) {
        return Database::get().execute_write([&](auto& txn) {
            return !txn.exec_params(
                           "UPDATE ani_agents SET state = 'running', pod_ip = $2 WHERE owner_id = $1::uuid AND state "
                           "= 'starting' AND pod_name IS NOT NULL RETURNING owner_id",
                           owner,
                           pod_ip)
                        .empty();
        });
    }

    /// starting|running -> stopping. @p reason lands in last_error when it is a failure (else empty).
    bool begin_stop(const std::string& owner, const std::string& error = "") {
        return Database::get().execute_write([&](auto& txn) {
            return !txn.exec_params(
                           "UPDATE ani_agents SET state = 'stopping', last_error = COALESCE(NULLIF($2, ''), "
                           "last_error) WHERE owner_id = $1::uuid AND state IN ('starting', 'running') RETURNING "
                           "owner_id",
                           owner,
                           error)
                        .empty();
        });
    }

    /// stopping -> stopped: the run's pod and tokens are gone, @p generation is the current snapshot.
    bool set_stopped(const std::string& owner, int generation) {
        return Database::get().execute_write([&](auto& txn) {
            return !txn.exec_params(
                           "UPDATE ani_agents SET state = 'stopped', generation = $2, pod_name = NULL, pod_ip = NULL, "
                           "gateway_token = NULL, proxy_token_hash = NULL WHERE owner_id = $1::uuid AND state = "
                           "'stopping' RETURNING owner_id",
                           owner,
                           generation)
                        .empty();
        });
    }

    /// The user whose running or starting pod holds the proxy token with this SHA-256.
    std::optional<std::string> owner_by_proxy_token(const std::string& token_hash) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<std::string> {
            auto r = txn.exec_params(
                "SELECT owner_id::text FROM ani_agents WHERE proxy_token_hash = $1 AND state IN ('starting', "
                "'running')",
                token_hash);
            if (r.empty()) {
                return std::nullopt;
            }
            return r[0][0].template as<std::string>();
        });
    }

    /// Every agent not stopped, with its ages: what the reconcile pass walks.
    nlohmann::json active() {
        return Database::get().execute_read([](auto& txn) {
            auto r = txn.exec("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                              " FROM ani_agents WHERE state <> 'stopped' ORDER BY owner_id) t");
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// Adds one model call to the user's month (UTC).
    void add_usage(const std::string& owner, long long prompt_tokens, long long completion_tokens) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO ani_usage (owner_id, month, requests, prompt_tokens, completion_tokens) "
                "VALUES ($1::uuid, date_trunc('month', now() AT TIME ZONE 'UTC')::date, 1, $2, $3) "
                "ON CONFLICT (owner_id, month) DO UPDATE SET requests = ani_usage.requests + 1, "
                "prompt_tokens = ani_usage.prompt_tokens + EXCLUDED.prompt_tokens, "
                "completion_tokens = ani_usage.completion_tokens + EXCLUDED.completion_tokens",
                owner,
                prompt_tokens,
                completion_tokens);
            return true;
        });
    }

    /// This month's calls and tokens of the user (zeros before the first call).
    nlohmann::json month_usage(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT to_char(date_trunc('month', now() AT TIME ZONE 'UTC'), 'YYYY-MM'), "
                "COALESCE(SUM(requests), 0), COALESCE(SUM(prompt_tokens), 0), COALESCE(SUM(completion_tokens), 0) "
                "FROM ani_usage WHERE owner_id = $1::uuid "
                "AND month = date_trunc('month', now() AT TIME ZONE 'UTC')::date",
                owner);
            return nlohmann::json{{"month", r[0][0].template as<std::string>()},
                                  {"requests", r[0][1].template as<long long>()},
                                  {"prompt_tokens", r[0][2].template as<long long>()},
                                  {"completion_tokens", r[0][3].template as<long long>()}};
        });
    }

private:
    static constexpr const char* kColumns =
        "owner_id::text AS owner_id, state, pod_name, pod_ip, generation, last_error, "
        "to_char(started_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS started_at, "
        "to_char(last_active_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS last_active_at, "
        "EXTRACT(EPOCH FROM now() - started_at)::bigint AS running_seconds, "
        "EXTRACT(EPOCH FROM now() - last_active_at)::bigint AS idle_seconds, "
        "EXTRACT(EPOCH FROM now() - updated_at)::bigint AS state_seconds";
};

}  // namespace Repositories::Agent
