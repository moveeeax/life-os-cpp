/**
 * @file GithubController.cpp
 * @brief Body for src/api/GithubController.hpp. The routes and guards are in
 *        place; each handler answers 501 not_implemented until the API lane
 *        fills it in against docs/openapi.yaml (tag github),
 *        repositories/github/*.hpp and github/Rules.hpp.
 */

#include "api/GithubController.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <variant>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "cache/Cache.hpp"
#include "github/Client.hpp"
#include "github/Settings.hpp"
#include "jobs/Jobs.hpp"
#include "net/Http.hpp"
#include "repositories/github/LinkRepository.hpp"
#include "security/Audit.hpp"
#include "security/Auth.hpp"
#include "security/SessionCookies.hpp"
#include "security/SessionStore.hpp"
#include "utils/BlockingPool.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"
#include "utils/Seal.hpp"

namespace Api {

namespace {

/// The stub answer of every handler below.
drogon::HttpResponsePtr not_implemented() {
    return ErrorResponse::make({drogon::k501NotImplemented, "not_implemented", "GitHub is not built yet"});
}

}  // namespace

namespace {

using json = nlohmann::json;
using Repositories::Github::LinkRepository;

/// The user signed in on this browser (the access cookie; SameSite=Lax rides the top-level return from
/// GitHub), or "" when there is none. The callback path is public, so the middleware did not resolve it.
std::string github_session_of(const HttpRequestPtr& req) {
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

drogon::HttpResponsePtr github_redirect(const std::string& outcome) {
    return drogon::HttpResponse::newRedirectionResponse("/account?github=" + outcome);
}

/// The Redis key that says a "Sync now" of the user was queued within the minute.
std::string github_sync_key(const std::string& owner) {
    return "github:sync:" + owner;
}

}  // namespace

void GithubController::status(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, owner);
    try {
        const bool on = Github::settings().has_value();
        const auto link = LinkRepository().status(owner);
        json data = link ? *link
                         : json{{"linked", false},
                                {"login", nullptr},
                                {"scopes", json::array()},
                                {"linked_at", nullptr},
                                {"synced_at", nullptr},
                                {"sync_error", nullptr},
                                {"org_hint", nullptr}};
        data["configured"] = on;
        callback(Response::ok(json{{"data", data}}));
    } catch (const std::exception& e) {
        spdlog::error("github.status failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void GithubController::oauthStart(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, owner);
    API_REFUSE_API_KEY(req, callback);
    const auto s = Github::settings();
    if (!s) {
        callback(ErrorResponse::not_found("github_not_configured"));
        return;
    }
    try {
        const std::string state = LinkRepository().new_state(owner);
        callback(drogon::HttpResponse::newRedirectionResponse(Github::auth_url(*s, state)));
    } catch (const std::exception& e) {
        spdlog::error("github.oauth.start failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void GithubController::oauthCallback(const HttpRequestPtr& req, Callback&& callback) {
    // Public: GitHub sends the browser here; the one-time state names the user, who must be the one signed in.
    // Never JSON: the browser lands on the Account page with the outcome.
    if (!Core::github_enabled()) {
        callback(github_redirect("error"));
        return;
    }
    const auto s = Github::settings();
    if (!s) {
        callback(github_redirect("error"));
        return;
    }
    std::optional<std::string> owner;
    try {
        owner = LinkRepository().take_state(req->getParameter("state"), github_session_of(req));
    } catch (const std::exception& e) {
        spdlog::error("github.oauth.callback: the state could not be read: {}", e.what());
        callback(github_redirect("error"));
        return;
    }
    if (!owner) {
        callback(github_redirect("expired"));
        return;
    }
    const std::string code = req->getParameter("code");
    if (!req->getParameter("error").empty() || code.empty()) {
        callback(github_redirect("denied"));
        return;
    }
    // The code exchange and the login lookup wait on GitHub: off the IO thread.
    auto respond = std::make_shared<Callback>(std::move(callback));
    const Github::Settings settings = *s;
    const std::string user = *owner;
    const bool queued = Utils::BlockingPool::shared().try_submit([respond, settings, user, code] {
        const Callback& reply = *respond;
        try {
            auto& t = Net::Http::transport();
            const auto tokens = Github::exchange(t, settings, code);
            if (!tokens.ok()) {
                spdlog::warn("github: the code exchange failed: {}", tokens.error);
                reply(github_redirect("error"));
                return;
            }
            auto login = Github::viewer_login(t, tokens.access_token);
            if (auto* f = std::get_if<Github::Failure>(&login)) {
                spdlog::warn("github: the viewer lookup failed: {} ({})", f->code, f->why);
                reply(github_redirect("error"));
                return;
            }
            const auto sealed = Utils::Seal::seal(tokens.access_token, settings.token_key);
            LinkRepository().set(user, std::get<std::string>(login), sealed.ciphertext, sealed.nonce, tokens.scopes);
            if (Jobs::is_initialized()) {
                try {
                    Jobs::get().submit(Github::kSyncJob, json{{"owner", user}});
                } catch (const std::exception& e) {
                    spdlog::warn("github: the first sync could not be queued: {}", e.what());
                }
            }
            Security::Audit::record(user, "github.link", "github_link", user);
            reply(github_redirect("ok"));
        } catch (const std::exception& e) {
            spdlog::error("github.oauth.callback failed: {}", e.what());
            reply(github_redirect("error"));
        }
    });
    if (!queued) {
        spdlog::warn("github.oauth.callback: the blocking pool is full");
        (*respond)(github_redirect("error"));
    }
}

void GithubController::disconnect(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, owner);
    API_REFUSE_API_KEY(req, callback);
    try {
        LinkRepository links;
        const auto link = links.get(owner);
        if (!link) {
            callback(ErrorResponse::not_found("github_not_linked"));
            return;
        }
        if (const auto s = Github::settings()) {
            try {
                // Best effort, bounded by the quick transport: deleting the link is what matters.
                Github::revoke(
                    Net::Http::quick_transport(), *s, Utils::Seal::unseal({link->sealed, link->nonce}, s->token_key));
            } catch (const Utils::Seal::SealError&) {
                // A token that does not open cannot be revoked anyway.
            }
        }
        links.remove(owner);
        if (Cache::is_initialized()) {
            Cache::get().del(github_sync_key(owner));
        }
        Security::Audit::record(owner, "github.unlink", "github_link", owner);
        no_content(callback);
    } catch (const std::exception& e) {
        spdlog::error("github.disconnect failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void GithubController::repos(const HttpRequestPtr& req, Callback&& callback) {
    // TODO(github api): 404 github_not_linked without a link; view (attention | all | archived, default attention,
    // else 400 invalid_query), owner, q, limit/offset (parse_page_params with kDefaultLimit/kMaxLimit);
    // RepoRepository::list -> {data, total, limit, offset, owners, counts}. Read keys allowed.
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, owner);
    (void)owner;
    callback(not_implemented());
}

void GithubController::repo(const HttpRequestPtr& req,
                            Callback&& callback,
                            const std::string& owner,
                            const std::string& name) {
    // TODO(github api): 404 github_not_linked without a link; RepoRepository::one(user, owner + "/" + name) or
    // 404 github_repo_not_found. Read keys allowed.
    (void)owner;
    (void)name;
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, user);
    (void)user;
    callback(not_implemented());
}

void GithubController::sync(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, owner);
    API_REFUSE_API_KEY(req, callback);
    try {
        LinkRepository links;
        if (!links.get(owner)) {
            callback(ErrorResponse::not_found("github_not_linked"));
            return;
        }
        API_REQUIRE_JOBS_READY(callback);
        const auto too_soon = [&](long long retry_after) {
            callback(ErrorResponse::make({drogon::k429TooManyRequests,
                                          "sync_too_soon",
                                          "a sync ran or was queued less than a minute ago",
                                          json{{"retry_after_sec", std::max(1LL, retry_after)}}}));
        };
        const auto since = links.seconds_since_sync(owner);
        if (since && *since < Github::kSyncMinSeconds) {
            too_soon(Github::kSyncMinSeconds - *since);
            return;
        }
        // A queued but unfinished sync counts too: the key lives a minute.
        if (Cache::is_initialized() &&
            !Cache::get().set_nx(github_sync_key(owner), "1", std::chrono::seconds(Github::kSyncMinSeconds))) {
            const long ttl = Cache::get().ttl(github_sync_key(owner));
            too_soon(ttl > 0 ? ttl : Github::kSyncMinSeconds);
            return;
        }
        Jobs::get().submit(Github::kSyncJob, json{{"owner", owner}});
        auto r = Response::ok(json{{"data", {{"status", "queued"}}}});
        r->setStatusCode(drogon::k202Accepted);
        callback(r);
    } catch (const std::exception& e) {
        spdlog::error("github.sync failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void GithubController::mute(const HttpRequestPtr& req,
                            Callback&& callback,
                            const std::string& owner,
                            const std::string& name,
                            const std::string& rule) {
    // TODO(github api): refuse API keys; 400 invalid_rule unless Github::is_rule(rule); 404 github_repo_not_found
    // unless RepoRepository::facts; MuteRepository::put, then set_attention(Github::attention(facts,
    // MuteRepository::of, now)); 200 {data: the row} (idempotent).
    (void)owner;
    (void)name;
    (void)rule;
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, user);
    (void)user;
    API_REFUSE_API_KEY(req, callback);
    callback(not_implemented());
}

void GithubController::unmute(const HttpRequestPtr& req,
                              Callback&& callback,
                              const std::string& owner,
                              const std::string& name,
                              const std::string& rule) {
    // TODO(github api): as mute, with MuteRepository::remove; 200 {data: the row} whether or not it was muted.
    (void)owner;
    (void)name;
    (void)rule;
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, user);
    (void)user;
    API_REFUSE_API_KEY(req, callback);
    callback(not_implemented());
}

void GithubController::makeTask(const HttpRequestPtr& req,
                                Callback&& callback,
                                const std::string& owner,
                                const std::string& name) {
    // TODO(github api): refuse API keys; body {rule} (400 invalid_rule); 404 github_repo_not_found; 404
    // github_not_linked; 409 not_needed? no: a rule not in the verdict still makes a task. task_of(rule): an
    // existing, not deleted task answers 201 {data: {task_id, created: false}} (clear_task when it is gone); else
    // create a task in Tasks ("<full_name>: <detail or rule>", note with https://github.com/<full_name>),
    // set_task, 201 {data: {task_id, created: true}}. Needs the tasks module on (404 tasks otherwise).
    (void)owner;
    (void)name;
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, user);
    (void)user;
    API_REFUSE_API_KEY(req, callback);
    callback(not_implemented());
}

}  // namespace Api
