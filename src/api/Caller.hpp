/**
 * @file Caller.hpp
 * @brief Who the caller is, for the handlers that need more than the
 *        middleware's principal: the user signed in on this browser on a
 *        public path, and the permission bits the data sources count with.
 */

#pragma once

#include <cstdint>
#include <exception>
#include <string>

#include <drogon/HttpRequest.h>

#include "domain/Role.hpp"
#include "security/Auth.hpp"
#include "security/SessionCookies.hpp"
#include "security/SessionStore.hpp"

namespace Api {

/// The user signed in on this browser (the access cookie, or a bearer token), or "" when
/// there is none. For public paths, where the middleware did not resolve a principal: an
/// OAuth return (SameSite=Lax rides the top-level redirect back from the provider).
inline std::string signed_in_user(const drogon::HttpRequestPtr& req) {
    if (!Security::Auth::is_initialized()) {
        return "";
    }
    const auto& auth = Security::Auth::get();
    const auto& cfg = auth.config();
    const std::string token = Security::Auth::extract_access_token(req, cfg.cookies);
    if (token.empty()) {
        return "";
    }
    try {
        std::string err;
        const auto p = auth.verify_jwt(token, err);
        if (!p || p->subject.empty() || Security::Sessions::access_revoked(cfg.cookies, p->subject, p->raw_claims)) {
            return "";
        }
        return p->subject;
    } catch (const std::exception&) {
        return "";
    }
}

/// The caller's permission bits, the fitness read bit set when the caller passes
/// that check (an admin, or no auth enforced), as /api/v1/metrics counts them.
inline std::uint32_t permissions_of(const drogon::HttpRequestPtr& req) {
    std::uint32_t p = Security::Auth::current_permissions(req);
    if (Security::Auth::require_permission(req, Domain::Permission::kFitnessRead) == nullptr) {
        p |= Domain::Permission::kFitnessRead;
    }
    return p;
}

}  // namespace Api
