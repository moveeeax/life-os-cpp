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

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <drogon/HttpRequest.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "security/Auth.hpp"
#include "utils/Crypto.hpp"

namespace Security::ApiKeys {

/// The data permissions a key of an administrator keeps once the admin bit is
/// masked: what the admin sentinel grants implicitly on the data routes.
/// GENERAL | FITNESS_READ | FITNESS_SYNC of src/domain/Role.hpp (security does
/// not depend on domain, so the value is spelled out as kAdminPermissionBits is).
inline constexpr std::uint32_t kAdminKeyDataBits = 0x01u | 0x04u | 0x08u;

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

/**
 * @brief Canonical segments of a request path, for the key-scope decision:
 *        percent-decoded once, backslashes read as slashes, lower-cased,
 *        empty and "." segments dropped, ".." resolved (never above the
 *        root). "/API//v1/./x/../Admin/users" -> {"api","v1","admin","users"}.
 *        The scope check runs in a sync advice BEFORE routing, so it cannot
 *        ask which route matched; it must read the path the way any router
 *        might, and err towards refusing.
 */
inline std::vector<std::string> canonical_segments(std::string_view path) {
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    std::string decoded;
    decoded.reserve(path.size());
    for (std::size_t i = 0; i < path.size(); ++i) {
        char c = path[i];
        if (c == '%' && i + 2 < path.size()) {
            const int hi = hex(path[i + 1]);
            const int lo = hex(path[i + 2]);
            if (hi >= 0 && lo >= 0) {
                c = static_cast<char>(hi * 16 + lo);
                i += 2;
            }
        }
        if (c == '\\')
            c = '/';
        decoded.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    std::vector<std::string> segs;
    std::size_t i = 0;
    while (i <= decoded.size()) {
        const auto j = std::min(decoded.find('/', i), decoded.size());
        const std::string seg = decoded.substr(i, j - i);
        if (seg == "..") {
            if (!segs.empty())
                segs.pop_back();
        } else if (!seg.empty() && seg != ".") {
            segs.push_back(seg);
        }
        i = j + 1;
    }
    return segs;
}

}  // namespace detail

/**
 * @brief Whether an API key may authenticate a request to @p path. Keys are
 *        for machine clients reading and writing the owner's data; they may
 *        not manage the account (password, email, the keys themselves under
 *        /api/v1/account/api-keys) or reach the admin surface. A leaked key
 *        must not be able to mint more keys or take over the account.
 *        The middleware answers 401 key_not_allowed on a refused path. The
 *        path is canonicalized first (detail::canonical_segments), so case,
 *        doubled slashes, dot segments and percent-encoding cannot slip a
 *        key past the check; the account / audit / key controllers also
 *        refuse a key principal themselves (API_REFUSE_API_KEY), and the
 *        admin guards see a key's permissions without the admin bit.
 */
inline bool path_allows_key(std::string_view path) {
    const auto segs = detail::canonical_segments(path);
    std::size_t idx = 0;
    if (idx < segs.size() && segs[idx] == "api")
        ++idx;
    if (idx < segs.size() && segs[idx].size() >= 2 && segs[idx][0] == 'v' &&
        std::all_of(segs[idx].begin() + 1, segs[idx].end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        ++idx;
    if (idx >= segs.size())
        return true;
    const std::string& area = segs[idx];
    return area != "account" && area != "admin" && !area.starts_with("api-keys");
}

/**
 * @brief Whether a key of scope "ani" may make this request: Ani's MCP
 *        endpoint only (/api/vN/ani/mcp, canonicalized as above). Any other
 *        path is answered 403 key_scope by the middleware.
 */
inline bool ani_key_allows(std::string_view path) {
    const auto segs = detail::canonical_segments(path);
    const bool version =
        segs.size() == 4 && segs[1].size() >= 2 && segs[1][0] == 'v' &&
        std::all_of(segs[1].begin() + 1, segs[1].end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    return version && segs[0] == "api" && segs[2] == "ani" && segs[3] == "mcp";
}

/**
 * @brief Whether a key of scope "read" may make this request: the safe
 *        methods only, and never a GET that writes. One does: a step of the
 *        Mi account link (/fitness/account/link/{id}) completes the link and
 *        enqueues a sync. A read key that tries to write is answered 403
 *        key_read_only by the middleware before any handler runs.
 */
inline bool read_key_allows(drogon::HttpMethod method, std::string_view path) {
    if (method != drogon::Get && method != drogon::Head && method != drogon::Options)
        return false;
    const auto segs = detail::canonical_segments(path);
    const auto has = [&](std::size_t i, std::string_view v) { return i < segs.size() && segs[i] == v; };
    for (std::size_t i = 0; i + 2 < segs.size(); ++i)
        if (has(i, "fitness") && has(i + 1, "account") && has(i + 2, "link"))
            return false;
    return true;
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
                "  SELECT k.id, k.user_id, k.scope, r.permissions "
                "  FROM api_keys k "
                "  JOIN users u ON u.id = k.user_id "
                "  JOIN roles r ON r.id = u.role_id "
                "  WHERE k.key_hash = $1 AND k.revoked_at IS NULL "
                "), bumped AS ("
                "  UPDATE api_keys SET last_used_at = now() "
                "  WHERE id = (SELECT id FROM authed) "
                "    AND (last_used_at IS NULL OR last_used_at < now() - make_interval(secs => $2)) "
                ") "
                "SELECT user_id, scope, permissions FROM authed",
                key_hash,
                kLastUsedThrottleSecs);
            if (r.empty())
                return std::nullopt;
            const auto user_id = r[0]["user_id"].template as<std::string>();
            // The owner's role permissions minus the admin bit: an admin's key
            // acts with the admin's data permissions, never as an admin. The
            // Administrator role holds only the sentinel bit (migration 004)
            // and passes every data gate through it, so a key of an admin is
            // given the data bits the sentinel stood for.
            const auto role = static_cast<std::uint32_t>(r[0]["permissions"].template as<long long>());
            const bool admin = (role & Security::Auth::kAdminPermissionBits) == Security::Auth::kAdminPermissionBits;
            const auto permissions = (role & ~Security::Auth::kAdminPermissionBits) | (admin ? kAdminKeyDataBits : 0u);
            const auto scope = r[0]["scope"].template as<std::string>();
            Security::Auth::AuthPrincipal p;
            p.subject = user_id;
            p.via_api_key = true;
            p.key_read_only = scope == "read";
            p.key_scope = scope;
            p.raw_claims =
                json{{"sub", user_id}, {"permissions", permissions}, {"api_key", true}, {"key_scope", scope}};
            return p;
        });
    } catch (const std::exception& e) {
        spdlog::warn("api-key auth: lookup failed ({})", e.what());
        return std::nullopt;
    }
}

}  // namespace Security::ApiKeys
