/**
 * @file ActionRepository.hpp
 * @brief ani_actions (migration 061): the changes Ani's agent proposed in a
 *        turn (agent/Actions.hpp). An action is pending until the user
 *        confirms or cancels it; a confirmed one is run by the worker and
 *        ends done or failed. A pending action older than kTtlSeconds reads
 *        as expired and can no longer be confirmed.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories::Agent {

class ActionRepository {
public:
    /// How long a proposal waits for the user.
    static constexpr int kTtlSeconds = 3600;

    /// Adds a pending action to the turn; returns its id.
    std::string add(const std::string& owner,
                    const std::string& turn_id,
                    const std::string& kind,
                    const std::string& summary,
                    const nlohmann::json& request) {
        return Database::get().execute_write([&](auto& txn) {
            return txn
                .exec_params(
                    "INSERT INTO ani_actions (owner_id, turn_id, kind, summary, request) VALUES ($1::uuid, "
                    "$2::uuid, $3, $4, $5::jsonb) RETURNING id::text",
                    owner,
                    turn_id,
                    kind,
                    summary,
                    request.dump())[0][0]
                .template as<std::string>();
        });
    }

    /// How many actions the turn proposed so far.
    int count_for_turn(const std::string& turn_id) {
        return Database::get().execute_read([&](auto& txn) {
            return txn.exec_params("SELECT count(*) FROM ani_actions WHERE turn_id::text = $1", turn_id)[0][0]
                .template as<int>();
        });
    }

    /// A new text for a pending Telegram proposal of the user (tg_send, tg_edit),
    /// changed before Confirm; false for any other action.
    bool retext(const std::string& owner, const std::string& id, const std::string& text) {
        return Database::get().execute_write([&](auto& txn) {
            return txn.exec_params(
                          "UPDATE ani_actions SET request = jsonb_set(request, '{body,text}', to_jsonb($3::text)), "
                          "summary = left(coalesce(request->>'label', '') || '«' || left($3, 300) || '»', 600), "
                          "updated_at = now() WHERE id::text = $1 AND owner_id = $2::uuid AND status = "
                          "'pending' AND kind IN ('tg_send', 'tg_edit')",
                          id,
                          owner,
                          text)
                       .affected_rows() > 0;
        });
    }

    enum class Answer { Done, NotPending, Expired, Missing };

    /// pending -> confirmed (when @p confirm) or cancelled, for the user's own action.
    Answer answer(const std::string& owner, const std::string& id, bool confirm) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "WITH a AS (SELECT status, created_at > now() - make_interval(secs => $4) AS fresh FROM ani_actions "
                "WHERE id::text = $1 AND owner_id = $2::uuid), "
                "u AS (UPDATE ani_actions SET status = $3 WHERE id::text = $1 AND owner_id = $2::uuid AND status = "
                "'pending' AND (created_at > now() - make_interval(secs => $4) OR $3 = 'cancelled') RETURNING id) "
                "SELECT (SELECT status FROM a), (SELECT fresh FROM a), (SELECT count(*) FROM u)",
                id,
                owner,
                std::string(confirm ? "confirmed" : "cancelled"),
                kTtlSeconds);
            if (r[0][0].is_null()) {
                return Answer::Missing;
            }
            if (r[0][2].template as<int>() > 0) {
                return Answer::Done;
            }
            const bool pending = r[0][0].template as<std::string>() == "pending";
            return pending && !r[0][1].template as<bool>() ? Answer::Expired : Answer::NotPending;
        });
    }

    /// One action of the user, as the panel shows it.
    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r =
                txn.exec_params("SELECT row_to_json(t) FROM (SELECT a.id::text AS id, a.kind, a.summary, a.error, " +
                                    message_sql() + " AS text, " + shown_status() +
                                    " AS status FROM ani_actions a WHERE a.id::text = $1 AND a.owner_id = $2::uuid) t",
                                id,
                                owner);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// What the worker needs: owner_id, status and the request. Any user's action.
    std::optional<nlohmann::json> for_worker(const std::string& id) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT json_build_object('owner_id', owner_id::text, 'status', status, 'request', request) FROM "
                "ani_actions WHERE id::text = $1",
                id);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// confirmed -> running. False when it is not confirmed (run already, or cancelled).
    bool start(const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            return !txn.exec_params(
                           "UPDATE ani_actions SET status = 'running' WHERE id::text = $1 AND status = 'confirmed' "
                           "RETURNING id",
                           id)
                        .empty();
        });
    }

    /// confirmed|running -> done, or failed with @p error when it is not empty.
    bool finish(const std::string& id, const std::string& error) {
        return Database::get().execute_write([&](auto& txn) {
            return !txn.exec_params(
                           "UPDATE ani_actions SET status = CASE WHEN $2 = '' THEN 'done' ELSE 'failed' END, error = "
                           "NULLIF($2, '') WHERE id::text = $1 AND status IN ('confirmed', 'running') RETURNING id",
                           id,
                           error)
                        .empty();
        });
    }

    /// The actions of the user's last turn before @p turn_id, as the agent is
    /// told of them: [{summary, status}], status as the panel reads it.
    nlohmann::json of_previous_turn(const std::string& owner, const std::string& turn_id) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(json_build_object('summary', a.summary, 'status', " + shown_status() +
                    ") ORDER BY a.created_at), '[]'::json) FROM ani_actions a WHERE a.turn_id = (SELECT p.id FROM "
                    "ani_turns p WHERE p.owner_id = $1::uuid AND p.created_at < (SELECT created_at FROM ani_turns "
                    "WHERE id::text = $2) ORDER BY p.created_at DESC LIMIT 1)",
                owner,
                turn_id);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// The status as shown, of an action aliased a: a pending one past kTtlSeconds is expired.
    static std::string shown_status() {
        return "CASE WHEN a.status = 'pending' AND a.created_at <= now() - interval '" + std::to_string(kTtlSeconds) +
               " seconds' THEN 'expired' ELSE a.status END";
    }

    /// The message of a Telegram proposal, which the person may change before
    /// Confirm; null for the other kinds.
    static std::string message_sql() {
        return "(CASE WHEN a.kind IN ('tg_send', 'tg_edit') THEN a.request #>> '{body,text}' END)";
    }

    /// The actions of a turn as a JSON array, for the turn's view; the outer
    /// query reads its turn from ani_turns.
    static std::string of_turn_sql() {
        return "(SELECT COALESCE(json_agg(json_build_object('id', a.id::text, 'kind', a.kind, 'summary', "
               "a.summary, 'status', " +
               shown_status() + ", 'error', a.error, 'text', " + message_sql() +
               ") ORDER BY a.created_at), '[]'::json) FROM ani_actions a WHERE a.turn_id = "
               "ani_turns.id) AS actions";
    }
};

}  // namespace Repositories::Agent
