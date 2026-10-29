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
    };

    /// The link, whoever it serves.
    std::optional<Link> current() {
        return Database::get().execute_read([&](auto& txn) -> std::optional<Link> {
            auto r = txn.exec("SELECT owner_id::text, me_id, username, primed FROM telegram_link LIMIT 1");
            if (r.empty()) {
                return std::nullopt;
            }
            return Link{r[0][0].template as<std::string>(),
                        r[0][1].template as<std::string>(),
                        r[0][2].template as<std::string>(),
                        r[0][3].template as<bool>()};
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
                "'last_error', last_error, 'created_at', created_at)::text FROM telegram_link WHERE owner_id = "
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
};

}  // namespace Repositories::Telegram
