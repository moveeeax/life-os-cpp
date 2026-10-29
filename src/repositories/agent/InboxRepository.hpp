/**
 * @file InboxRepository.hpp
 * @brief ani_inbox (migration 062): what came through the user's webhook,
 *        waiting for the Now page. say, outfit and act items are ready at
 *        once; an event is thinking until its agent turn ends: then the
 *        oldest item of the turn carries her reply (ready) and the rest are
 *        delivered, or all are delivered when she chose silence, or failed.
 *        The page claims ready items oldest first, each once.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories::Agent {

class InboxRepository {
public:
    /// A sender's id is taken once in this window.
    static constexpr int kDedupeSeconds = 86400;
    /// Ended rows are deleted after this long.
    static constexpr int kKeepSeconds = 7 * 86400;

    struct AddResult {
        std::string id;
        bool duplicate = false;
        /// A reminder's time is past or more than a week ahead: nothing stored.
        bool bad_time = false;
    };

    /// When a reminder is due (one of delay or at) and where an answer goes.
    struct Extra {
        std::optional<long long> delay_seconds;
        std::optional<std::string> at;
        std::optional<std::string> callback;
    };

    enum class Answer { Ok, Unknown, NotAsk, Answered, BadOption, Expired };

    /// A question shown on the page stays open at least this long for the click.
    static constexpr int kAskShownSeconds = 15 * 60;

    /// An ask whose answer (or end unanswered) is to be sent to its callback.
    struct Callback {
        std::string id;
        std::string owner;
        std::optional<std::string> source_id;
        std::string question;
        std::optional<std::string> answer;
        std::string url;
    };

    /// Stores an item (@p status "ready" or "thinking"). With @p source_id
    /// already used by the user within kDedupeSeconds: that item's id, duplicate.
    AddResult add(const std::string& owner,
                  const std::string& kind,
                  const nlohmann::json& payload,
                  const std::optional<std::string>& source_id,
                  int ttl_seconds,
                  const std::string& status,
                  const Extra& extra = {}) {
        return Database::get().execute_write([&](auto& txn) {
            // One writer per user at a time: the duplicate check holds.
            txn.exec_params("SELECT pg_advisory_xact_lock(hashtext('ani_inbox:' || $1))", owner);
            if (source_id) {
                auto d = txn.exec_params(
                    "SELECT id::text FROM ani_inbox WHERE owner_id = $1::uuid AND source_id = $2 AND created_at > "
                    "now() - make_interval(secs => $3) ORDER BY created_at LIMIT 1",
                    owner,
                    *source_id,
                    kDedupeSeconds);
                if (!d.empty()) {
                    return AddResult{d[0][0].template as<std::string>(), true};
                }
            }
            const bool spoken = kind == "say" || kind == "remind" || kind == "ask";
            const std::optional<std::string> say =
                spoken ? std::optional<std::string>(payload.value("text", std::string())) : std::nullopt;
            // A reminder is due in delay seconds or at a time, within a week from now.
            std::optional<std::string> not_before;
            if (extra.delay_seconds || extra.at) {
                auto t = txn.exec_params(
                    "SELECT CASE WHEN t BETWEEN now() - interval '1 minute' AND now() + interval '7 days 1 minute' "
                    "THEN t::text END FROM (SELECT COALESCE(now() + make_interval(secs => $1::bigint), "
                    "$2::timestamptz) AS t) x",
                    extra.delay_seconds,
                    extra.at);
                if (t[0][0].is_null()) {
                    return AddResult{"", false, true};
                }
                not_before = t[0][0].template as<std::string>();
            }
            auto r = txn.exec_params(
                "INSERT INTO ani_inbox (owner_id, kind, payload, source_id, status, say, ready_at, not_before, "
                "callback, expires_at) VALUES ($1::uuid, $2, $3::jsonb, $4, $5, $6, CASE WHEN $5 = 'ready' THEN "
                "now() END, $8::timestamptz, $9, COALESCE($8::timestamptz, now()) + make_interval(secs => $7)) "
                "RETURNING id::text",
                owner,
                kind,
                payload.dump(),
                source_id,
                status,
                say,
                ttl_seconds,
                not_before,
                extra.callback);
            return AddResult{r[0][0].template as<std::string>(), false};
        });
    }

    /// Items not played or ended yet (queued, thinking, ready, unexpired): the cap.
    int pending_count(const std::string& owner) {
        return count_(owner, "('queued', 'thinking', 'ready') AND (not_before IS NULL OR not_before <= now())");
    }

    /// Ready items the page has not claimed: the dot on her ledge.
    int waiting(const std::string& owner) {
        return count_(owner, "('ready') AND (not_before IS NULL OR not_before <= now())");
    }

    /// Up to @p limit ready items, oldest first, marked delivered; only of
    /// @p kinds when given. A question handed out stays open for its click
    /// at least kAskShownSeconds.
    nlohmann::json claim(const std::string& owner, int limit, const std::vector<std::string>& kinds = {}) {
        std::optional<std::string> only;
        if (!kinds.empty()) {
            std::string list = "{";
            for (const auto& k : kinds) {
                list += (list.size() > 1 ? "," : "") + k;
            }
            only = list + "}";
        }
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "WITH c AS (SELECT id FROM ani_inbox WHERE owner_id = $1::uuid AND status = 'ready' AND expires_at > "
                "now() AND (not_before IS NULL OR not_before <= now()) AND ($3::text[] IS NULL OR kind = "
                "ANY($3::text[])) "
                "AND NOT EXISTS (SELECT 1 FROM ani_prefs p WHERE p.owner_id = $1::uuid AND p.hold_until > now()) "
                "ORDER BY COALESCE(not_before, created_at), id LIMIT $2 FOR UPDATE SKIP LOCKED), "
                "u AS (UPDATE ani_inbox i SET status = 'delivered', delivered_at = now(), expires_at = CASE WHEN "
                "i.kind = 'ask' THEN GREATEST(i.expires_at, now() + make_interval(secs => $4)) ELSE i.expires_at "
                "END FROM c WHERE i.id = c.id RETURNING i.id, i.kind, i.say, i.payload, i.created_at) "
                "SELECT COALESCE(json_agg(json_build_object('id', id::text, 'kind', kind, 'say', say, 'payload', "
                "payload, 'created_at', " +
                    iso("created_at") + ") ORDER BY created_at, id), '[]'::json) FROM u",
                owner,
                limit,
                only,
                kAskShownSeconds);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// The user's thinking events no turn has taken yet, oldest first:
    /// [{id, payload, expires_in}].
    nlohmann::json thinking_unlinked(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(json_build_object('id', id::text, 'payload', payload, 'created_at', " +
                    iso("created_at") +
                    ", 'expires_in', EXTRACT(EPOCH FROM expires_at - now())::bigint) ORDER BY created_at, id), "
                    "'[]'::json) FROM ani_inbox WHERE owner_id = $1::uuid AND status = 'thinking' AND turn_id IS "
                    "NULL",
                owner);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    void link(const std::vector<std::string>& ids, const std::string& turn_id) {
        Database::get().execute_write([&](auto& txn) {
            for (const auto& id : ids) {
                txn.exec_params(
                    "UPDATE ani_inbox SET turn_id = $2::uuid WHERE id::text = $1 AND status = 'thinking'", id, turn_id);
            }
            return true;
        });
    }

    /// The turn's items end: the oldest carries @p say (ready), the others are
    /// delivered; without a line (she chose silence) all are delivered.
    void resolve_turn(const std::string& turn_id, const std::optional<std::string>& say) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "WITH f AS (SELECT id FROM ani_inbox WHERE turn_id = $1::uuid AND status = 'thinking' ORDER BY "
                "created_at, id LIMIT 1) "
                "UPDATE ani_inbox i SET status = CASE WHEN $2::text IS NOT NULL AND i.id IN (SELECT id FROM f) THEN "
                "'ready' ELSE 'delivered' END, say = CASE WHEN i.id IN (SELECT id FROM f) THEN $2::text END, "
                "ready_at = now(), delivered_at = CASE WHEN $2::text IS NOT NULL AND i.id IN (SELECT id FROM f) "
                "THEN NULL ELSE now() END, "
                // The spoken item names its turn: the page shows the turn's proposals as buttons.
                "payload = CASE WHEN i.id IN (SELECT id FROM f) THEN i.payload || jsonb_build_object('turn', "
                "$1::text) ELSE i.payload END WHERE i.turn_id = $1::uuid AND i.status = 'thinking'",
                turn_id,
                say);
            return true;
        });
    }

    /// The turn's thinking items fail (the agent could not answer).
    void fail_turn(const std::string& turn_id) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params("UPDATE ani_inbox SET status = 'failed' WHERE turn_id = $1::uuid AND status = 'thinking'",
                            turn_id);
            return true;
        });
    }

    /// Thinking items of the user that no turn took: they fail (the agent
    /// cannot take them before they expire).
    void fail_unlinked(const std::string& owner) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE ani_inbox SET status = 'failed' WHERE owner_id = $1::uuid AND status = 'thinking' AND "
                "turn_id IS NULL",
                owner);
            return true;
        });
    }

    /// The user's last @p limit items for the log in her settings: time,
    /// kind, status, who sent it (via), her line and an ask's answer.
    nlohmann::json recent(const std::string& owner, int limit) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT id::text AS id, kind, CASE WHEN status IN "
                "('queued', 'thinking', 'ready') AND expires_at <= now() THEN 'expired' ELSE status END AS status, "
                "payload->>'via' AS via, say, answer, " +
                    iso("created_at") +
                    " AS created_at FROM ani_inbox WHERE owner_id = $1::uuid ORDER BY created_at DESC, id LIMIT $2) t",
                owner,
                limit);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// The user's last @p limit items with what became of them, for her
    /// MCP: [{id, kind, status, say, payload, created_at}], newest first.
    nlohmann::json items(const std::string& owner, int limit) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + item_columns() +
                                         " FROM ani_inbox WHERE owner_id = $1::uuid ORDER BY created_at DESC, id "
                                         "LIMIT $2) t",
                                     owner,
                                     limit);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// One of the user's items as items() shows it.
    std::optional<nlohmann::json> item(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params("SELECT row_to_json(t)::text FROM (SELECT " + item_columns() +
                                         " FROM ani_inbox WHERE owner_id = $1::uuid AND id::text = $2) t",
                                     owner,
                                     id);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// The user's answer to an ask: one of its options, once.
    Answer answer(const std::string& owner, const std::string& id, const std::string& text) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT kind, answer IS NOT NULL, payload->'options' ? $3, expires_at <= now() OR callback_sent "
                "FROM ani_inbox WHERE owner_id = $1::uuid AND id::text = $2 FOR UPDATE",
                owner,
                id,
                text);
            if (r.empty()) {
                return Answer::Unknown;
            }
            if (r[0][0].template as<std::string>() != "ask") {
                return Answer::NotAsk;
            }
            if (r[0][1].template as<bool>()) {
                return Answer::Answered;
            }
            if (r[0][3].template as<bool>()) {
                return Answer::Expired;  // its sender was told it ran out
            }
            if (!r[0][2].template as<bool>()) {
                return Answer::BadOption;
            }
            txn.exec_params("UPDATE ani_inbox SET answer = $2, answered_at = now() WHERE id::text = $1", id, text);
            return Answer::Ok;
        });
    }

    /// Asks with a callback that are answered or ran out unanswered and not
    /// sent yet; marked sent as they are read, so each goes once.
    std::vector<Callback> due_callbacks(int limit = 100) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "UPDATE ani_inbox SET callback_sent = true WHERE id IN (SELECT id FROM ani_inbox WHERE kind = 'ask' "
                "AND callback IS NOT NULL AND NOT callback_sent AND (answer IS NOT NULL OR expires_at <= now()) "
                "ORDER BY created_at LIMIT $1 FOR UPDATE SKIP LOCKED) RETURNING id::text, owner_id::text, "
                "source_id, payload->>'text', answer, callback",
                limit);
            std::vector<Callback> out;
            for (const auto& row : r) {
                Callback c;
                c.id = row[0].template as<std::string>();
                c.owner = row[1].template as<std::string>();
                if (!row[2].is_null()) {
                    c.source_id = row[2].template as<std::string>();
                }
                c.question = row[3].is_null() ? "" : row[3].template as<std::string>();
                if (!row[4].is_null()) {
                    c.answer = row[4].template as<std::string>();
                }
                c.url = row[5].template as<std::string>();
                out.push_back(std::move(c));
            }
            return out;
        });
    }

    /// Puts a callback back to be sent again (its job could not be queued).
    void unsend(const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params("UPDATE ani_inbox SET callback_sent = false WHERE id::text = $1", id);
            return true;
        });
    }

    /// Marks unplayed items past their time expired and deletes ended rows
    /// older than kKeepSeconds; returns how many rows changed or went.
    int purge() {
        return Database::get().execute_write([&](auto& txn) {
            const auto expired = txn.exec(
                "UPDATE ani_inbox SET status = 'expired' WHERE status IN ('queued', 'thinking', 'ready') AND "
                "expires_at <= now() RETURNING id");
            const auto gone = txn.exec_params(
                "DELETE FROM ani_inbox WHERE status IN ('delivered', 'expired', 'failed') AND created_at < now() - "
                "make_interval(secs => $1) RETURNING id",
                kKeepSeconds);
            return static_cast<int>(expired.size() + gone.size());
        });
    }

private:
    static std::string item_columns() {
        return "id::text AS id, kind, CASE WHEN status IN ('queued', 'thinking', 'ready') AND expires_at <= now() "
               "THEN 'expired' ELSE status END AS status, say, payload, answer, " +
               iso("not_before") + " AS not_before, " + iso("created_at") + " AS created_at";
    }

    static std::string iso(const std::string& col) {
        return "to_char(" + col + " AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"')";
    }

    int count_(const std::string& owner, const std::string& statuses) {
        return Database::get().execute_read([&](auto& txn) {
            return txn
                .exec_params(
                    "SELECT count(*) FROM ani_inbox WHERE owner_id = $1::uuid AND expires_at > now() AND "
                    "status IN " +
                        statuses,
                    owner)[0][0]
                .template as<int>();
        });
    }
};

}  // namespace Repositories::Agent
