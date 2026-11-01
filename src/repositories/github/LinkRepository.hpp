/**
 * @file LinkRepository.hpp
 * @brief github_links (migration 079): a user's sealed GitHub token, the
 *        login and scopes GitHub granted, and the last sync's outcome. The
 *        one-time states of the OAuth rounds live in Redis (ten minutes,
 *        bound to the user), not in a table.
 */

#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cache/Cache.hpp"
#include "database/Database.hpp"
#include "github/Settings.hpp"
#include "repositories/SqlTime.hpp"
#include "utils/Crypto.hpp"

namespace Repositories::Github {

class LinkRepository {
public:
    static constexpr int kStateSeconds = ::Github::kStateSeconds;

    struct Link {
        std::string login;
        std::string sealed;
        std::string nonce;
        /// Space-separated, as GitHub's X-OAuth-Scopes / the token answer gives it ("repo,read:org" normalised to
        /// spaces).
        std::string scopes;
        std::optional<std::string> synced_at;  // ISO 8601 UTC
        std::optional<std::string> sync_error;
        std::string org_hint;  // "" when none
    };

    struct Linked {
        std::string owner;
        std::string login;
    };

    /// A new one-time state of @p owner, kept in Redis for kStateSeconds. @throws std::runtime_error without Redis.
    std::string new_state(const std::string& owner) {
        if (!Cache::is_initialized()) {
            throw std::runtime_error("github: the cache is not up, no OAuth state");
        }
        const std::string state = Utils::Crypto::random_hex(24);
        if (!Cache::get().set(state_key(state), owner, kStateSeconds)) {
            throw std::runtime_error("github: the cache did not keep the OAuth state");
        }
        return state;
    }

    /// The user of a fresh @p state, which is used up; nullopt when unknown or old.
    std::optional<std::string> take_state(const std::string& state) {
        if (state.empty() || state.size() > 64 || !Cache::is_initialized()) {
            return std::nullopt;
        }
        const auto owner = Cache::get().get(state_key(state));
        if (!owner) {
            return std::nullopt;
        }
        Cache::get().del(state_key(state));
        return owner;
    }

    /// take_state, and only for @p session (the user signed in on the browser that came back); spent either way.
    std::optional<std::string> take_state(const std::string& state, const std::string& session) {
        auto owner = take_state(state);
        if (!owner || session.empty() || *owner != session) {
            return std::nullopt;
        }
        return owner;
    }

    std::optional<Link> get(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<Link> {
            auto r =
                txn.exec_params("SELECT login, token_sealed, token_nonce, scopes, " SQL_ISO_UTC(
                                    "synced_at") ", sync_error, org_hint FROM github_links WHERE owner_id = $1::uuid",
                                owner);
            if (r.empty()) {
                return std::nullopt;
            }
            Link l;
            l.login = r[0][0].template as<std::string>();
            l.sealed = r[0][1].template as<std::string>();
            l.nonce = r[0][2].template as<std::string>();
            l.scopes = r[0][3].template as<std::string>();
            if (!r[0][4].is_null()) {
                l.synced_at = r[0][4].template as<std::string>();
            }
            if (!r[0][5].is_null()) {
                l.sync_error = r[0][5].template as<std::string>();
            }
            l.org_hint = r[0][6].template as<std::string>();
            return l;
        });
    }

    /// {linked: true, login, scopes: [..], linked_at, synced_at, sync_error, org_hint} of the user's link
    /// (the GithubStatus of the API without `configured`); nullopt when not linked.
    std::optional<nlohmann::json> status(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT json_build_object('linked', true, 'login', login, 'scopes', "
                "COALESCE(string_to_array(NULLIF(scopes, ''), ' '), '{}'::text[]), 'linked_at', " SQL_ISO_UTC(
                    "linked_at") ", 'synced_at', " SQL_ISO_UTC("synced_at") ", 'sync_error', sync_error, 'org_hint', "
                                 "NULLIF(org_hint, ''))::text FROM github_links WHERE owner_id = $1::uuid",
                owner);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// Links (or links again: a new token, the sync error and the org hint cleared, the repositories kept).
    void set(const std::string& owner,
             const std::string& login,
             const std::string& sealed,
             const std::string& nonce,
             const std::string& scopes) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO github_links (owner_id, login, token_sealed, token_nonce, scopes) VALUES ($1::uuid, $2, "
                "$3, $4, $5) ON CONFLICT (owner_id) DO UPDATE SET login = EXCLUDED.login, token_sealed = "
                "EXCLUDED.token_sealed, token_nonce = EXCLUDED.token_nonce, scopes = EXCLUDED.scopes, sync_error = "
                "NULL, org_hint = '', linked_at = now()",
                owner,
                login,
                sealed,
                nonce,
                scopes);
            return true;
        });
    }

    /// A sync that worked: synced_at = now, no error, @p org_hint ("" when every org answered).
    void mark_synced(const std::string& owner, const std::string& org_hint) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE github_links SET synced_at = now(), sync_error = NULL, org_hint = $2 WHERE owner_id = $1::uuid",
                owner,
                org_hint);
            return true;
        });
    }

    /// A sync that failed: the error kept (200 characters), synced_at and the repositories as they were.
    void mark_failed(const std::string& owner, const std::string& why) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params("UPDATE github_links SET sync_error = left($2, 200) WHERE owner_id = $1::uuid", owner, why);
            return true;
        });
    }

    /// Seconds since the last sync; nullopt when never synced or not linked. For the once-a-minute "Sync now".
    std::optional<long long> seconds_since_sync(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<long long> {
            auto r = txn.exec_params(
                "SELECT extract(epoch FROM now() - synced_at)::bigint FROM github_links WHERE owner_id = $1::uuid",
                owner);
            if (r.empty() || r[0][0].is_null()) {
                return std::nullopt;
            }
            return r[0][0].template as<long long>();
        });
    }

    /// Forgets the link; the repositories and mutes go with it. True when there was one.
    bool remove(const std::string& owner) {
        return Database::get().execute_write([&](auto& txn) {
            txn.exec_params("DELETE FROM github_mutes WHERE owner_id = $1::uuid", owner);
            txn.exec_params("DELETE FROM github_repos WHERE owner_id = $1::uuid", owner);
            return txn.exec_params("DELETE FROM github_links WHERE owner_id = $1::uuid", owner).affected_rows() > 0;
        });
    }

    /// Every linked user, oldest link first (the job walks them).
    std::vector<Linked> all() {
        return Database::get().execute_read([&](auto& txn) {
            std::vector<Linked> out;
            for (const auto& row : txn.exec("SELECT owner_id::text, login FROM github_links ORDER BY linked_at")) {
                out.push_back({row[0].template as<std::string>(), row[1].template as<std::string>()});
            }
            return out;
        });
    }

private:
    static std::string state_key(const std::string& state) { return "github:oauth:" + state; }
};

}  // namespace Repositories::Github
