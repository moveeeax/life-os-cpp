/**
 * @file Guards.hpp
 * @brief Uniform handler-entry guards shared by all controllers.
 * @details Expressed as macros so each guard is a single line that can't be
 *          half-written (the `callback(err); return;` pair stays atomic) and
 *          adding a new handler can't silently skip the check. Prefixed with
 *          API_ because they intentionally stay defined for every includer.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpResponse.h>

#include "api/RequestUtils.hpp"
#include "security/Auth.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

/// Reject the request with 403 unless the principal is a full admin.
/// No-op when auth is disabled (AUTH_MODE=none) — same as require_admin.
#define API_REQUIRE_ADMIN(req, callback)                            \
    do {                                                            \
        if (auto _guard_err = Security::Auth::require_admin(req)) { \
            callback(_guard_err);                                   \
            return;                                                 \
        }                                                           \
    } while (0)

/// Reject with 403 unless the principal holds `perm` (a Domain::Permission bit).
/// No-op when auth is disabled. For fine-grained gates beyond full-admin: a
/// principal with the admin sentinel bit (Security::Auth::kAdminPermissionBits,
/// 0x40000000) passes every check, Auth::current_user_can short-circuits on it.
#define API_REQUIRE_PERMISSION(req, callback, perm)                            \
    do {                                                                       \
        if (auto _guard_err = Security::Auth::require_permission(req, perm)) { \
            callback(_guard_err);                                              \
            return;                                                            \
        }                                                                      \
    } while (0)

/// Resolve the authenticated principal into `var` (std::optional) or
/// reject with 401. Use in handlers that need the caller's identity.
#define API_REQUIRE_PRINCIPAL(req, callback, var)    \
    auto var = Security::Auth::principal_of(req);    \
    do {                                             \
        if (!(var)) {                                \
            callback(ErrorResponse::unauthorized()); \
            return;                                  \
        }                                            \
    } while (0)

/// Bind the caller's user id (principal subject) into `var` (std::string) for
/// owner-scoped resources, or reject with 401. Unlike API_REQUIRE_ADMIN this is
/// NOT a no-op when auth is disabled: a per-user resource is meaningless without
/// an identity, so owner-scoped endpoints genuinely require AUTH_MODE != none.
/// Pass `var` as the owner_id to CrudBase find_owned/list_owned/count_owned.
#define API_REQUIRE_OWNER(req, callback, var)              \
    std::string var;                                       \
    do {                                                   \
        auto _owner_p = Security::Auth::principal_of(req); \
        if (!_owner_p || _owner_p->subject.empty()) {      \
            callback(ErrorResponse::unauthorized());       \
            return;                                        \
        }                                                  \
        (var) = _owner_p->subject;                         \
    } while (0)

/// Reject with 401 key_not_allowed when the request authenticated with an API
/// key. For the account / key-management / audit handlers: the middleware
/// already refuses keys on those paths, this is the same rule decided on the
/// matched route instead of the raw path.
#define API_REFUSE_API_KEY(req, callback)                                                     \
    do {                                                                                      \
        if (auto _key_p = Security::Auth::principal_of(req); _key_p && _key_p->via_api_key) { \
            callback(ErrorResponse::unauthorized("key_not_allowed"));                         \
            return;                                                                           \
        }                                                                                     \
    } while (0)

/// Reject with 403 unless the caller's email is confirmed (401 if anonymous).
/// No-op when auth is disabled. The confirmed flag is minted into the access
/// JWT but NOT enforced by default — gate your domain's confirmation-required
/// routes with this.
#define API_REQUIRE_CONFIRMED(req, callback)                            \
    do {                                                                \
        if (auto _guard_err = Security::Auth::require_confirmed(req)) { \
            callback(_guard_err);                                       \
            return;                                                     \
        }                                                               \
    } while (0)

/// Reject with 404 `<module_name>` while a feature module is switched off: its
/// routes stay registered, the module reads as absent.
#define API_REQUIRE_MODULE(flag_expr, module_name, callback) \
    do {                                                     \
        if (!(flag_expr)) {                                  \
            callback(ErrorResponse::not_found(module_name)); \
            return;                                          \
        }                                                    \
    } while (0)

/// API_REQUIRE_OWNER, then 403 no_user_account unless `owner` is an app user
/// id: a static-bearer principal has no user, and per-user rows need one.
/// Declares `owner` in the method body, hence no do/while around it.
#define API_REQUIRE_USER(req, callback, owner)       \
    API_REQUIRE_OWNER(req, callback, owner);         \
    do {                                             \
        if (!::Api::require_user(owner, callback)) { \
            return;                                  \
        }                                            \
    } while (0)

/// The entry of a per-user module route: 404 `<module_name>` while the module
/// is off, then API_REQUIRE_OWNER (401), then the no_user_account 403.
#define API_MODULE_USER_GUARD(flag_expr, module_name, req, callback, owner) \
    API_REQUIRE_MODULE(flag_expr, module_name, callback);                   \
    API_REQUIRE_USER(req, callback, owner)

/// API_MODULE_USER_GUARD with a permission check between the module switch
/// and the identity (fitness, workout): 404, then 403 on `perm`, then 401,
/// then the no_user_account 403.
#define API_MODULE_PERMISSION_USER_GUARD(flag_expr, module_name, req, callback, perm, owner) \
    API_REQUIRE_MODULE(flag_expr, module_name, callback);                                    \
    API_REQUIRE_PERMISSION(req, callback, perm);                                             \
    API_REQUIRE_USER(req, callback, owner)

/// Reject with 503 when the job queue is disabled. The includer must also
/// include jobs/Jobs.hpp — the macro expands Jobs:: at the use site.
#define API_REQUIRE_JOBS_READY(callback)                                                            \
    do {                                                                                            \
        if (!Jobs::is_initialized()) {                                                              \
            callback(ErrorResponse::service_unavailable("jobs_disabled", "Job queue not enabled")); \
            return;                                                                                 \
        }                                                                                           \
    } while (0)

namespace Api {

/// 403 no_user_account unless @p owner is an app user id (a static-bearer
/// principal has none). Returns false after responding.
inline bool require_user(const std::string& owner,
                         const std::function<void(const drogon::HttpResponsePtr&)>& callback) {
    if (is_valid_uuid(owner)) {
        return true;
    }
    callback(ErrorResponse::forbidden("no_user_account", "this route needs a user account"));
    return false;
}

/// Reject a path-parameter id that isn't a canonical 8-4-4-4-12 UUID with the
/// shared 400 shape. Returns false after responding — callers
/// `if (!require_valid_uuid(id, callback)) return;`.
inline bool require_valid_uuid(const std::string& id,
                               const std::function<void(const drogon::HttpResponsePtr&)>& callback) {
    if (is_valid_uuid(id))
        return true;
    callback(ErrorResponse::bad_request("invalid_uuid", "UUID format is invalid"));
    return false;
}

}  // namespace Api
