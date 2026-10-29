/**
 * @file LinkRepository.hpp
 * @brief calendar_links and calendar_states (migration 068): a user's sealed
 *        Google refresh token and the one-time states of the consent rounds.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "utils/Crypto.hpp"

namespace Repositories::Calendar {

class LinkRepository {
public:
    static constexpr int kStateSeconds = 600;

    struct Link {
        std::string email;
        std::string sealed;
        std::string nonce;
        bool broken = false;
    };

    /// A new one-time state of @p owner (old ones of anyone are swept).
    std::string new_state(const std::string& owner) {
        const std::string state = Utils::Crypto::random_hex(24);
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params("DELETE FROM calendar_states WHERE created_at < now() - make_interval(secs => $1)",
                            kStateSeconds);
            txn.exec_params("INSERT INTO calendar_states (state, owner_id) VALUES ($1, $2::uuid)", state, owner);
            return true;
        });
        return state;
    }

    /// The user of a fresh @p state, which is used up; nullopt when unknown or old.
    std::optional<std::string> take_state(const std::string& state) {
        return Database::get().execute_write([&](auto& txn) -> std::optional<std::string> {
            auto r = txn.exec_params(
                "DELETE FROM calendar_states WHERE state = $1 RETURNING owner_id::text, created_at > now() - "
                "make_interval(secs => $2)",
                state,
                kStateSeconds);
            if (r.empty() || !r[0][1].template as<bool>()) {
                return std::nullopt;
            }
            return r[0][0].template as<std::string>();
        });
    }

    std::optional<Link> get(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<Link> {
            auto r = txn.exec_params(
                "SELECT email, token_sealed, token_nonce, broken_at IS NOT NULL FROM calendar_links WHERE owner_id "
                "= $1::uuid",
                owner);
            if (r.empty()) {
                return std::nullopt;
            }
            return Link{r[0][0].template as<std::string>(),
                        r[0][1].template as<std::string>(),
                        r[0][2].template as<std::string>(),
                        r[0][3].template as<bool>()};
        });
    }

    /// {email, broken, created_at} of the user's link.
    std::optional<nlohmann::json> status(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT json_build_object('email', email, 'broken', broken_at IS NOT NULL, 'created_at', "
                "created_at)::text FROM calendar_links WHERE owner_id = $1::uuid",
                owner);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// Links (or links again, which clears broken).
    void set(const std::string& owner, const std::string& email, const std::string& sealed, const std::string& nonce) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO calendar_links (owner_id, email, token_sealed, token_nonce) VALUES ($1::uuid, $2, $3, "
                "$4) ON CONFLICT (owner_id) DO UPDATE SET email = EXCLUDED.email, token_sealed = "
                "EXCLUDED.token_sealed, token_nonce = EXCLUDED.token_nonce, broken_at = NULL",
                owner,
                email,
                sealed,
                nonce);
            return true;
        });
    }

    /// Marks the link broken; true only the first time (the caller says it once).
    bool mark_broken(const std::string& owner) {
        return Database::get().execute_write([&](auto& txn) {
            return txn.exec_params(
                          "UPDATE calendar_links SET broken_at = now() WHERE owner_id = $1::uuid AND broken_at IS "
                          "NULL",
                          owner)
                       .affected_rows() > 0;
        });
    }

    bool remove(const std::string& owner) {
        return Database::get().execute_write([&](auto& txn) {
            return txn.exec_params("DELETE FROM calendar_links WHERE owner_id = $1::uuid", owner).affected_rows() > 0;
        });
    }

    /// Unix seconds of @p date at @p hour:00 in @p tz (hour may be 24 for the next midnight).
    long long local_instant(const std::string& date, int hour, const std::string& tz) {
        return Database::get().execute_read([&](auto& txn) {
            return txn
                .exec_params(
                    "SELECT extract(epoch FROM (($1::date + make_interval(hours => $2)) AT TIME ZONE "
                    "$3))::bigint",
                    date,
                    hour,
                    tz)[0][0]
                .template as<long long>();
        });
    }
};

}  // namespace Repositories::Calendar
