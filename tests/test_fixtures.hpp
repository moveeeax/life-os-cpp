/**
 * @file test_fixtures.hpp
 * @brief Reusable test scenarios — JWTs and principals.
 *
 * test_helpers.hpp already covers infrastructure plumbing (config, reset,
 * connectivity checks). This file adds *domain* fixtures that several test
 * files would otherwise build by hand.
 *
 * Keep fixtures small and explicit: every helper has a clear "what scenario
 * does this represent." Avoid building a 'god fixture' that pulls in every
 * subsystem — pick the smallest set the test actually needs.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "security/Auth.hpp"
#include "utils/Base64.hpp"
#include "utils/Crypto.hpp"

namespace TestFixtures {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// JWT helpers — call these to mint tokens for security/auth tests without
// duplicating the base64url + HMAC dance in every file.
// ---------------------------------------------------------------------------

inline int64_t now_sec() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

inline std::string hmac256_raw(const std::string& key, const std::string& data) {
    return Utils::Crypto::hmac_sha256(key, data);
}

/// Build a compact-serialized HS256 JWT from claims.
inline std::string make_jwt(const json& claims, const std::string& secret) {
    json header = {{"alg", "HS256"}, {"typ", "JWT"}};
    std::string h = Utils::Base64::url_encode(header.dump());
    std::string p = Utils::Base64::url_encode(claims.dump());
    std::string signing = h + "." + p;
    std::string sig = Utils::Base64::url_encode(Utils::Crypto::hmac_sha256(secret, signing));
    return signing + "." + sig;
}

// ---------------------------------------------------------------------------
// Auth principal builders — for testing controllers or middleware that read
// `req->attributes()->get<AuthPrincipal>(kPrincipalAttr)`.
// ---------------------------------------------------------------------------

inline Security::Auth::AuthPrincipal admin_principal(const std::string& sub = "admin-user") {
    Security::Auth::AuthPrincipal p;
    p.subject = sub;
    p.roles = {"admin", "user"};
    p.scopes = {};
    // require_admin/require_permission read the "permissions" claim from
    // raw_claims — without it this principal is rejected now that fixtures
    // default to auth.mode=jwt.
    p.raw_claims = json{{"sub", sub}, {"permissions", Security::Auth::kAdminPermissionBits}};
    return p;
}

inline Security::Auth::AuthPrincipal user_principal(const std::string& sub = "regular-user") {
    Security::Auth::AuthPrincipal p;
    p.subject = sub;
    p.roles = {"user"};
    p.scopes = {};
    p.raw_claims = json{{"sub", sub}, {"permissions", 1}};  // 0x01 = GENERAL
    return p;
}

}  // namespace TestFixtures
