/**
 * @file AuthController.hpp
 * @brief Auth endpoints: register, login, logout, refresh, me.
 *
 * Behaviour:
 *   - Returns JSON, not HTML. The frontend handles redirects.
 *   - Cookie-based session (HttpOnly + SameSite=Lax).
 *   - Refresh-token rotation: every /refresh returns a brand-new refresh JWT
 *     and invalidates the previous JTI in Redis. Logout deletes the JTI.
 *   - Email-confirmation token generation lives here so /register can fire
 *     it; the actual SMTP send is wired in stage 2 (AccountController +
 *     Mailer). Until then we log the link at INFO level.
 *
 * Declarations only — the handler bodies live in AuthController.cpp
 * (compiled once into app_core; docs/ARCHITECTURE.md §4). The route
 * macros (ADD_METHOD_TO) must stay in this header: Drogon's METHOD_LIST
 * registration is part of the class definition, and
 * scripts/check-routes-registered.sh greps the src/api headers for them.
 */

#pragma once

#include <functional>
#include <optional>
#include <string>

#include <drogon/HttpController.h>

#include <nlohmann/json_fwd.hpp>

#include "domain/User.hpp"
#include "security/Auth.hpp"

namespace Api {

using namespace drogon;
using json = nlohmann::json;

class AuthController : public HttpController<AuthController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::registerUser, "/api/v1/auth/register", Post);
    ADD_METHOD_TO(AuthController::registration, "/api/v1/auth/registration", Get);
    ADD_METHOD_TO(AuthController::login, "/api/v1/auth/login", Post);
    ADD_METHOD_TO(AuthController::logout, "/api/v1/auth/logout", Post);
    ADD_METHOD_TO(AuthController::refresh, "/api/v1/auth/refresh", Post);
    ADD_METHOD_TO(AuthController::me, "/api/v1/auth/me", Get);
    METHOD_LIST_END

    // ---------------------------------------------------------------------
    // POST /api/auth/register
    //
    // Body: { email, password, first_name?, last_name? }
    // Behaviour: creates an unconfirmed user, generates a confirm-email
    // token, and (stage 2) emails it. NOT auto-login: the
    // user has to click the link, then log in.
    // ---------------------------------------------------------------------
    void registerUser(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);

    // ---------------------------------------------------------------------
    // GET /api/v1/auth/registration
    //
    // Auth-public probe: {"data": {"open": bool}}. The sign-in page asks it
    // to decide whether to show the sign-up link. With
    // auth.registration_open = false, POST /register answers 403
    // registration_closed; admin invites (POST /admin/invite, then
    // /account/join-from-invite/{token}) keep working.
    // ---------------------------------------------------------------------
    void registration(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);

    // ---------------------------------------------------------------------
    // POST /api/auth/login
    //
    // Body: { email, password }
    // Returns: { user } + Set-Cookie access/refresh.
    // Generic 401 on either wrong email or wrong password (no user
    // enumeration).
    // ---------------------------------------------------------------------
    void login(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);

    // ---------------------------------------------------------------------
    // POST /api/auth/logout
    //
    // Reads refresh-token cookie, deletes its JTI from Redis (so further
    // /refresh calls fail), and zeroes both cookies. Auth-public
    // (kDefaultPublicPathsCsv): it must work after the access cookie expired,
    // and it answers 200 even when there is no session at all.
    // ---------------------------------------------------------------------
    void logout(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);

    // ---------------------------------------------------------------------
    // POST /api/auth/refresh
    //
    // Reads refresh-token cookie, verifies it, checks the JTI is still
    // live in Redis, rotates: new access + new refresh (with new JTI),
    // deletes the old JTI. Returns the user payload.
    // ---------------------------------------------------------------------
    void refresh(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);

    // ---------------------------------------------------------------------
    // GET /api/auth/me
    //
    // Returns the authenticated user. Requires a valid access token (the
    // global auth middleware already gates the path). 401 if missing /
    // expired; 404 if the user row vanished mid-session.
    // ---------------------------------------------------------------------
    void me(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);

    struct Session {
        std::string access;
        std::string refresh;
    };

    /**
     * @brief Mint access + refresh JWTs, write refresh JTI to Redis.
     *        Returns nullopt if Redis write fails — refresh would be
     *        unverifiable, so we'd rather refuse the login than mint a
     *        permanently-invalid session. Public for AccountController's
     *        password change, which re-issues the caller's own session after
     *        revoking all of them.
     */
    static std::optional<Session> mint_session(const Domain::User& user);

private:
    static std::string make_jti();

    /// auth.registration_open / REGISTRATION_OPEN, read per request (default true).
    static bool registration_open();

    /// True when the refresh token carries an integer `exp` that has not
    /// passed (with the configured leeway). A token without `exp` is refused.
    static bool refresh_unexpired(const json& claims, int leeway_sec);

    /// True only for the one caller that removed the live JTI (see Sessions::consume).
    static bool consume_refresh(const Security::Auth::AuthConfig& cfg, const std::string& jti);

    static void revoke_jti(const Security::Auth::AuthConfig& cfg, const std::string& jti);

    static void revoke_refresh(const Security::Auth::AuthConfig& cfg, const std::string& refresh_token);
};

}  // namespace Api
