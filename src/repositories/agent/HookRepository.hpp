/**
 * @file HookRepository.hpp
 * @brief ani_hooks (migration 062): each user's webhook token. The token is
 *        kept as is (the settings page shows the URL at any time). It lets
 *        anyone who holds it make Ani say, wear or play something, and hand
 *        an event to the user's agent. That webhook turn reads the person's
 *        data with her read tools, but changes nothing by itself: it is
 *        offered only the proposals the person confirms (agent/Tools.hpp
 *        may_offer), runs in a session apart from the chat, and the
 *        event's text is defanged (agent/Hook.hpp).
 */

#pragma once

#include <optional>
#include <string>

#include "database/Database.hpp"
#include "utils/Crypto.hpp"

namespace Repositories::Agent {

class HookRepository {
public:
    static constexpr const char* kPrefix = "aniwh_";

    /// The user's token, made on first need.
    std::string ensure(const std::string& owner) {
        return Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO ani_hooks (owner_id, token) VALUES ($1::uuid, $2) ON CONFLICT (owner_id) "
                "DO NOTHING",
                owner,
                fresh());
            return txn.exec_params("SELECT token FROM ani_hooks WHERE owner_id = $1::uuid", owner)[0][0]
                .template as<std::string>();
        });
    }

    /// A new token; the old one stops working at once.
    std::string rotate(const std::string& owner) {
        const std::string token = fresh();
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO ani_hooks (owner_id, token) VALUES ($1::uuid, $2) ON CONFLICT (owner_id) DO UPDATE "
                "SET token = EXCLUDED.token, rotated_at = now()",
                owner,
                token);
            return true;
        });
        return token;
    }

    /// The owner of @p token; nullopt for one that never existed or was replaced.
    std::optional<std::string> owner_of(const std::string& token) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<std::string> {
            auto r = txn.exec_params("SELECT owner_id::text FROM ani_hooks WHERE token = $1", token);
            if (r.empty()) {
                return std::nullopt;
            }
            return r[0][0].template as<std::string>();
        });
    }

    void touch(const std::string& owner) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params("UPDATE ani_hooks SET last_used_at = now() WHERE owner_id = $1::uuid", owner);
            return true;
        });
    }

private:
    static std::string fresh() { return std::string(kPrefix) + Utils::Crypto::random_hex(24); }
};

}  // namespace Repositories::Agent
