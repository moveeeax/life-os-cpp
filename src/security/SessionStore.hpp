/**
 * @file SessionStore.hpp
 * @brief Refresh-token revocation store (Redis), shared by the auth and
 *        account controllers.
 * @details Each issued refresh token has a JTI written to
 *          `<prefix><jti> = "1"` with the refresh TTL; /refresh consumes it,
 *          logout deletes it. To support "log out everywhere" on a password
 *          change/reset we ALSO index every JTI in a per-user set
 *          `<prefix>user:<sub>` so revoke_all() can sweep them.
 *
 *          Access JWTs are stateless, so revoke_all() also writes
 *          `<prefix>since:<sub>` = the revocation time in ms; the auth advice
 *          refuses an access token of that user issued earlier
 *          (access_revoked()). The marker lives one access TTL plus a margin:
 *          after that every older access token has expired on its own.
 *
 *          All ops are best-effort (Redis fail-open); callers that need the
 *          fail-closed signal (mint_session) check the bool return.
 */

#pragma once

#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "cache/Cache.hpp"
#include "security/Auth.hpp"
#include "utils/Time.hpp"

namespace Security::Sessions {

inline std::string jti_key(const Auth::CookieConfig& cfg, const std::string& jti) {
    return cfg.refresh_revocation_prefix + jti;
}

inline std::string user_set_key(const Auth::CookieConfig& cfg, const std::string& sub) {
    return cfg.refresh_revocation_prefix + "user:" + sub;
}

inline std::string valid_since_key(const Auth::CookieConfig& cfg, const std::string& sub) {
    return cfg.refresh_revocation_prefix + "since:" + sub;
}

/// How long past the access TTL the "valid since" marker stays: covers the
/// verifier's exp leeway (30 s by default) with room to spare.
inline constexpr int kValidSinceMarginSec = 300;

/// The access token's issue time in ms: the iat_ms claim mint_session writes,
/// else iat (seconds) * 1000 for a token without it. nullopt when neither is
/// an integer.
inline std::optional<std::int64_t> issued_at_ms(const Auth::json& claims) {
    if (const auto it = claims.find("iat_ms"); it != claims.end() && it->is_number_integer())
        return it->get<std::int64_t>();
    if (const auto it = claims.find("iat"); it != claims.end() && it->is_number_integer())
        return it->get<std::int64_t>() * 1000;
    return std::nullopt;
}

/**
 * @brief Stamp "every access token of @p sub issued before now is void".
 *        Called by revoke_all(); best-effort like the rest of the store.
 */
inline void mark_access_revoked(const Auth::CookieConfig& cfg, const std::string& sub) {
    if (!Cache::is_initialized())
        return;
    const std::string now_ms = std::to_string(Utils::Time::now_epoch_millis());
    if (!Cache::get().set(valid_since_key(cfg, sub), now_ms, cfg.access_ttl_sec + kValidSinceMarginSec))
        spdlog::warn("sessions: could not write the valid-since marker; access tokens of {} live out their TTL", sub);
}

/**
 * @brief True when the access token with @p claims was issued before the
 *        user's sessions were revoked. A token without an issue time is
 *        refused once a marker exists.
 *
 *        Fail-open: no cache, a Redis error (Cache::get answers nullopt) or an
 *        unreadable marker count as "not revoked". The alternative is worse
 *        for the same outage: fail-closed would answer 401 to every signed-in
 *        request while Redis is away, and the SPA's refresh fails then as
 *        well (consume() is fail-closed), so a Redis blip would sign every
 *        user out. Fail-open keeps the window this check closes at what it
 *        was before the check existed (one access TTL, 15 min), and only
 *        while Redis is down: no refresh can mint a new token meanwhile.
 */
inline bool access_revoked(const Auth::CookieConfig& cfg, const std::string& sub, const Auth::json& claims) {
    if (sub.empty() || !Cache::is_initialized())
        return false;
    const auto since = Cache::get().get(valid_since_key(cfg, sub));
    if (!since)
        return false;
    std::int64_t since_ms = 0;
    try {
        since_ms = std::stoll(*since);
    } catch (const std::exception&) {
        return false;
    }
    const auto iat_ms = issued_at_ms(claims);
    return !iat_ms || *iat_ms < since_ms;
}

/**
 * @brief Record a freshly-issued refresh JTI as live + index it under the
 *        user. Returns false if the live-marker write failed (Redis down) —
 *        mint_session treats that as fail-closed.
 */
inline bool record(const Auth::CookieConfig& cfg, const std::string& sub, const std::string& jti, int ttl_sec) {
    if (!Cache::is_initialized())
        return false;
    if (!Cache::get().set(jti_key(cfg, jti), "1", ttl_sec))
        return false;
    // Index for revoke_all. Best-effort: a missed sadd only means this JTI
    // won't be swept by a future "log out everywhere" — the per-JTI marker
    // above (and its TTL) still governs validity.
    try {
        auto& redis = Cache::get().get_client();
        const std::string set_key = user_set_key(cfg, sub);
        redis.sadd(set_key, jti);
        // Keep the set from outliving the longest possible session.
        redis.expire(set_key, ttl_sec);
    } catch (...) {}
    return true;
}

/**
 * @brief Take a live refresh JTI out of the store, atomically: true only for
 *        the one caller whose delete removed it. A replay, or a second refresh
 *        racing with the same token, gets false. Fails closed (false) when
 *        the cache is down.
 */
inline bool consume(const Auth::CookieConfig& cfg, const std::string& jti) {
    try {
        if (!Cache::is_initialized())
            return false;
        return Cache::get().del(jti_key(cfg, jti)) > 0;
    } catch (...) {
        return false;
    }
}

inline void revoke_jti(const Auth::CookieConfig& cfg, const std::string& jti) {
    try {
        if (Cache::is_initialized())
            Cache::get().del(jti_key(cfg, jti));
    } catch (...) {}
}

/**
 * @brief Revoke every session of a user — password reset/change, invite
 *        redeem, role change, account delete. Deletes the refresh JTIs (no new
 *        access token can be minted) and stamps the valid-since marker, so the
 *        access tokens already out are refused by the auth advice from the
 *        next request on (while Redis answers; see access_revoked()).
 */
inline void revoke_all(const Auth::CookieConfig& cfg, const std::string& sub) {
    if (!Cache::is_initialized())
        return;
    mark_access_revoked(cfg, sub);
    try {
        auto& redis = Cache::get().get_client();
        std::vector<std::string> jtis;
        redis.smembers(user_set_key(cfg, sub), std::back_inserter(jtis));
        for (const auto& jti : jtis) {
            try {
                redis.del(jti_key(cfg, jti));
            } catch (...) {}
        }
        redis.del(user_set_key(cfg, sub));
    } catch (...) {}
}

}  // namespace Security::Sessions
