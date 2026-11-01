/**
 * @file LinkRepository.hpp
 * @brief telegram_link and telegram_cursors (migration 067): the one user the
 *        owner's Telegram account serves, and the latest message the poll has
 *        looked at in each chat.
 */

#pragma once

#include <map>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories::Telegram {

class LinkRepository {
public:
    struct Link {
        std::string owner;
        std::string me_id;
        std::string username;
        bool primed = false;
        bool review_to_self = false;
        /// The waiting pass ran within the hour.
        bool waiting_fresh = false;
        /// The morning and evening line go as a voice message to Saved Messages.
        bool voice_to_self = false;
    };

    /// The link, whoever it serves.
    std::optional<Link> current() {
        return Database::get().execute_read([&](auto& txn) -> std::optional<Link> {
            auto r = txn.exec(
                "SELECT owner_id::text, me_id, username, primed, review_to_self, COALESCE(waiting_at > now() - "
                "interval '1 hour', false), voice_to_self FROM telegram_link LIMIT 1");
            if (r.empty()) {
                return std::nullopt;
            }
            return Link{r[0][0].template as<std::string>(),
                        r[0][1].template as<std::string>(),
                        r[0][2].template as<std::string>(),
                        r[0][3].template as<bool>(),
                        r[0][4].template as<bool>(),
                        r[0][5].template as<bool>(),
                        r[0][6].template as<bool>()};
        });
    }

    /// The link when it serves @p owner.
    std::optional<Link> of(const std::string& owner) {
        auto l = current();
        if (!l || l->owner != owner) {
            return std::nullopt;
        }
        return l;
    }

    /// {me_id, username, polled_at, last_error, created_at} of @p owner's link.
    std::optional<nlohmann::json> status(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT json_build_object('me_id', me_id, 'username', username, 'polled_at', polled_at, "
                "'last_error', last_error, 'created_at', created_at, 'review_to_self', review_to_self, "
                "'voice_to_self', "
                "voice_to_self)::text FROM "
                "telegram_link WHERE owner_id = "
                "$1::uuid",
                owner);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// Links @p owner; false when the account already serves someone else.
    /// Linking again keeps the cursors; a new owner starts from scratch.
    bool set(const std::string& owner, const std::string& me_id, const std::string& username) {
        return Database::get().execute_write([&](auto& txn) {
            auto other = txn.exec_params("SELECT 1 FROM telegram_link WHERE owner_id <> $1::uuid", owner);
            if (!other.empty()) {
                return false;
            }
            txn.exec_params(
                "INSERT INTO telegram_link (owner_id, me_id, username) VALUES ($1::uuid, $2, $3) "
                "ON CONFLICT (owner_id) DO UPDATE SET me_id = EXCLUDED.me_id, username = EXCLUDED.username, "
                "last_error = NULL",
                owner,
                me_id,
                username);
            return true;
        });
    }

    bool remove(const std::string& owner) {
        return Database::get().execute_write([&](auto& txn) {
            txn.exec_params("DELETE FROM telegram_cursors WHERE owner_id = $1::uuid", owner);
            txn.exec_params("DELETE FROM telegram_waiting WHERE owner_id = $1::uuid", owner);
            return txn.exec_params("DELETE FROM telegram_link WHERE owner_id = $1::uuid", owner).affected_rows() > 0;
        });
    }

    /// The cursor of each chat the poll has looked at.
    std::map<std::string, long long> cursors(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            std::map<std::string, long long> out;
            for (const auto& row :
                 txn.exec_params("SELECT chat_id, last_id FROM telegram_cursors WHERE owner_id = $1::uuid", owner)) {
                out[row[0].template as<std::string>()] = row[1].template as<long long>();
            }
            return out;
        });
    }

    void set_cursor(const std::string& owner, const std::string& chat, long long last_id) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO telegram_cursors (owner_id, chat_id, last_id) VALUES ($1::uuid, $2, $3) "
                "ON CONFLICT (owner_id, chat_id) DO UPDATE SET last_id = GREATEST(telegram_cursors.last_id, "
                "EXCLUDED.last_id), updated_at = now()",
                owner,
                chat,
                last_id);
            return true;
        });
    }

    /// A poll went through (and set the cursors when it was the first).
    void polled(const std::string& owner) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE telegram_link SET polled_at = now(), primed = true, last_error = NULL WHERE owner_id = "
                "$1::uuid",
                owner);
            return true;
        });
    }

    void failed(const std::string& owner, const std::string& error) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE telegram_link SET last_error = left($2, 200) WHERE owner_id = $1::uuid", owner, error);
            return true;
        });
    }

    /// The private chats waiting for an answer, replaced as one set (the hourly pass).
    void set_waiting(const std::string& owner, const nlohmann::json& chats) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params("DELETE FROM telegram_waiting WHERE owner_id = $1::uuid", owner);
            for (const auto& c : chats) {
                txn.exec_params(
                    "INSERT INTO telegram_waiting (owner_id, chat_id, title, since) VALUES ($1::uuid, $2, left($3, "
                    "200), to_timestamp($4)) ON CONFLICT DO NOTHING",
                    owner,
                    c.value("chat", std::string()),
                    c.value("title", std::string()),
                    c.value("since", 0LL));
            }
            txn.exec_params("UPDATE telegram_link SET waiting_at = now() WHERE owner_id = $1::uuid", owner);
            return true;
        });
    }

    /// [{chat, title, since}] oldest first, quiet chats left out.
    nlohmann::json waiting(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            return nlohmann::json::parse(
                txn.exec_params(
                       "SELECT COALESCE(json_agg(json_build_object('chat', w.chat_id, 'title', w.title, 'since', "
                       "w.since) ORDER BY w.since), '[]'::json)::text FROM telegram_waiting w WHERE w.owner_id = "
                       "$1::uuid AND NOT EXISTS (SELECT 1 FROM telegram_quiet q WHERE q.owner_id = w.owner_id AND "
                       "q.chat_id = w.chat_id)",
                       owner)[0][0]
                    .template as<std::string>());
        });
    }

    std::map<std::string, std::string> quiet(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            std::map<std::string, std::string> out;
            for (const auto& row :
                 txn.exec_params("SELECT chat_id, title FROM telegram_quiet WHERE owner_id = $1::uuid", owner)) {
                out[row[0].template as<std::string>()] = row[1].template as<std::string>();
            }
            return out;
        });
    }

    void set_quiet(const std::string& owner, const std::string& chat, const std::string& title, bool on) {
        Database::get().execute_write([&](auto& txn) {
            if (on) {
                txn.exec_params(
                    "INSERT INTO telegram_quiet (owner_id, chat_id, title) VALUES ($1::uuid, $2, left($3, 200)) ON "
                    "CONFLICT (owner_id, chat_id) DO UPDATE SET title = EXCLUDED.title",
                    owner,
                    chat,
                    title);
            } else {
                txn.exec_params("DELETE FROM telegram_quiet WHERE owner_id = $1::uuid AND chat_id = $2", owner, chat);
            }
            return true;
        });
    }

    void set_review_to_self(const std::string& owner, bool on) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params("UPDATE telegram_link SET review_to_self = $2 WHERE owner_id = $1::uuid", owner, on);
            return true;
        });
    }

    /// Marks @p week (ISO, "2026-W44") as sent; false when it went already.
    bool claim_review_week(const std::string& owner, const std::string& week) {
        return Database::get().execute_write([&](auto& txn) {
            return txn.exec_params(
                          "UPDATE telegram_link SET review_sent_week = $2 WHERE owner_id = $1::uuid AND "
                          "review_to_self AND review_sent_week IS DISTINCT FROM $2",
                          owner,
                          week)
                       .affected_rows() > 0;
        });
    }

    void set_voice_to_self(const std::string& owner, bool on) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params("UPDATE telegram_link SET voice_to_self = $2 WHERE owner_id = $1::uuid", owner, on);
            return true;
        });
    }

    /// Marks the @p kind line ("morning", "evening") of @p day as sent; false when it went already or the
    /// switch is off.
    bool claim_voice(const std::string& owner, const std::string& kind, const std::string& day) {
        return Database::get().execute_write([&](auto& txn) {
            return txn.exec_params(
                          "UPDATE telegram_link SET voice_sent = voice_sent || jsonb_build_object($2::text, $3::text) "
                          "WHERE owner_id = $1::uuid AND voice_to_self AND (voice_sent->>$2) IS DISTINCT FROM $3",
                          owner,
                          kind,
                          day)
                       .affected_rows() > 0;
        });
    }

    /// Gives the @p kind line of @p day back after the voice or Telegram failed,
    /// so the next poll of that hour tries again (as release_review_week).
    void release_voice(const std::string& owner, const std::string& kind, const std::string& day) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE telegram_link SET voice_sent = voice_sent - $2::text WHERE owner_id = $1::uuid AND "
                "(voice_sent->>$2) = $3",
                owner,
                kind,
                day);
            return true;
        });
    }

    /// Gives @p week back after Telegram refused the review, so the next try can send it.
    void release_review_week(const std::string& owner, const std::string& week) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE telegram_link SET review_sent_week = NULL WHERE owner_id = $1::uuid AND review_sent_week = $2",
                owner,
                week);
            return true;
        });
    }
};

}  // namespace Repositories::Telegram
