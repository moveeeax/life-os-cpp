/**
 * @file ApiKeys.hpp
 * @brief Authenticate machine clients by API key / personal access token.
 *
 * A key is `cpk_<64 hex>` — high entropy, so we store only its SHA-256 hash
 * (a fast hash is fine; unlike passwords there's nothing to brute-force). The
 * request layer hashes the presented key and looks it up. The plaintext is
 * returned to the user exactly once, at creation (see ApiKeyController).
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <drogon/HttpRequest.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "security/Auth.hpp"
#include "utils/Crypto.hpp"

namespace Security::ApiKeys {

using json = nlohmann::json;

/// Distinguishes an API key from a JWT in the Authorization header.
inline constexpr const char* kPrefix = "cpk_";

struct GeneratedKey {
    std::string plaintext;  // shown to the user ONCE — never stored
    std::string key_hash;   // sha256_hex(plaintext) — what the DB stores
    std::string prefix;     // first chars, for later display
};

/// Mint a new key. The caller stores key_hash + prefix and returns plaintext once.
inline GeneratedKey generate() {
    GeneratedKey g;
    g.plaintext = std::string(kPrefix) + Utils::Crypto::random_hex(32);  // 256-bit secret
    g.key_hash = Utils::Crypto::sha256_hex(g.plaintext);
    g.prefix = g.plaintext.substr(0, 12);  // e.g. "cpk_a1b2c3d4"
    return g;
}

/// Pull a `cpk_`-prefixed credential from the request: the X-API-Key header, or
/// an `Authorization: Bearer <key>` / `ApiKey <key>` whose token has the prefix.
/// Returns "" when none is present (so the caller falls through to JWT/cookie).
inline std::string extract_key(const drogon::HttpRequestPtr& req) {
    const std::string& x = req->getHeader("X-API-Key");
    if (!x.empty())
        return x;
    const std::string& authz_header = req->getHeader("Authorization");
    const std::string_view authz{authz_header};
    constexpr std::string_view key_prefix{kPrefix};
    for (const std::string_view scheme : {std::string_view{"Bearer "}, std::string_view{"ApiKey "}}) {
        if (authz.size() > scheme.size() && authz.starts_with(scheme)) {
            const std::string_view tok = authz.substr(scheme.size());
            if (tok.starts_with(key_prefix))
                return std::string(tok);
        }
    }
    return "";
}

inline bool request_has_key(const drogon::HttpRequestPtr& req) {
    return !extract_key(req).empty();
}

namespace detail {

/// @p path is @p root itself or lies under it ("/x" matches "/x" and "/x/y",
/// not "/xy").
inline bool path_under(std::string_view path, std::string_view root) {
    if (!path.starts_with(root))
        return false;
    return path.size() == root.size() || path[root.size()] == '/';
}

}  // namespace detail

/**
 * @brief Whether an API key may authenticate a request to @p path. Keys are
 *        for machine clients reading and writing the owner's data; they may
 *        not manage the account (password, email, the keys themselves under
 *        /api/v1/account/api-keys) or reach the admin surface. A leaked key
 *        must not be able to mint more keys or take over the account.
 *        The middleware answers 401 key_not_allowed on a refused path.
 */
inline bool path_allows_key(std::string_view path) {
    return !detail::path_under(path, "/api/v1/account") && !detail::path_under(path, "/api/v1/admin") &&
           !detail::path_under(path, "/api/v1/api-keys");
}

/**
 * @brief Authenticate the request's API key. Returns a principal carrying the
 *        owning user's subject + role permissions (with the admin bit masked
 *        out, via_api_key set), or nullopt if the key is absent / unknown /
 *        revoked.
 *
 * Runs on PRIMARY (a just-issued key must authenticate immediately — no replica
 * lag). last_used_at is stamped THROTTLED: a single CTE always returns the auth
 * row but only UPDATEs the timestamp when it is older than kLastUsedThrottleSecs,
 * so a high-QPS key costs a read + a no-op update (0 rows, no WAL) on the common
 * path instead of an indexed-row write per request.
 */
inline constexpr int kLastUsedThrottleSecs = 60;

inline std::optional<Security::Auth::AuthPrincipal> authenticate(const drogon::HttpRequestPtr& req) {
    const std::string key = extract_key(req);
    if (key.empty())
        return std::nullopt;
    const std::string key_hash = Utils::Crypto::sha256_hex(key);

    try {
        return Database::get().execute_write([&](auto& txn) -> std::optional<Security::Auth::AuthPrincipal> {
            auto r = txn.exec_params(
                "WITH authed AS ("
                "  SELECT k.id, k.user_id, r.permissions "
                "  FROM api_keys k "
                "  JOIN users u ON u.id = k.user_id "
                "  JOIN roles r ON r.id = u.role_id "
                "  WHERE k.key_hash = $1 AND k.revoked_at IS NULL "
                "), bumped AS ("
                "  UPDATE api_keys SET last_used_at = now() "
                "  WHERE id = (SELECT id FROM authed) "
                "    AND (last_used_at IS NULL OR last_used_at < now() - make_interval(secs => $2)) "
                ") "
                "SELECT user_id, permissions FROM authed",
                key_hash,
                kLastUsedThrottleSecs);
            if (r.empty())
                return std::nullopt;
            const auto user_id = r[0]["user_id"].template as<std::string>();
            // The owner's role permissions minus the admin bit: an admin's key
            // acts with the admin's data permissions, never as an admin.
            const auto permissions = static_cast<std::uint32_t>(r[0]["permissions"].template as<long long>()) &
                                     ~Security::Auth::kAdminPermissionBits;
            Security::Auth::AuthPrincipal p;
            p.subject = user_id;
            p.via_api_key = true;
            p.raw_claims = json{{"sub", user_id}, {"permissions", permissions}, {"api_key", true}};
            return p;
        });
    } catch (const std::exception& e) {
        spdlog::warn("api-key auth: lookup failed ({})", e.what());
        return std::nullopt;
    }
}

}  // namespace Security::ApiKeys
