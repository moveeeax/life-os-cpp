/**
 * @file GithubController.cpp
 * @brief Body for src/api/GithubController.hpp: the OAuth link, the sync
 *        request, the repository lists, mutes and tasks of the GitHub
 *        section (docs/openapi.yaml, tag github).
 */

#include "api/GithubController.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "cache/Cache.hpp"
#include "github/Client.hpp"
#include "github/Rules.hpp"
#include "github/Settings.hpp"
#include "jobs/Jobs.hpp"
#include "net/Http.hpp"
#include "repositories/github/LinkRepository.hpp"
#include "repositories/github/MuteRepository.hpp"
#include "repositories/github/RepoRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "security/Audit.hpp"
#include "security/Auth.hpp"
#include "security/SessionCookies.hpp"
#include "security/SessionStore.hpp"
#include "tasks/Fields.hpp"
#include "utils/BlockingPool.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"
#include "utils/Seal.hpp"
#include "utils/Time.hpp"

namespace Api {

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

// ── repositories, mutes and tasks ───────────────────────────────────────────

namespace {

using json = nlohmann::json;
namespace GithubRepos = Repositories::Github;

const std::vector<std::string> kViews = {"attention", "all", "archived"};

/// 404 github_not_linked unless the user linked their account. Returns false after responding.
bool require_link(const std::string& user, const std::function<void(const HttpResponsePtr&)>& callback) {
    if (GithubRepos::LinkRepository().get(user)) {
        return true;
    }
    callback(ErrorResponse::not_found("github_not_linked"));
    return false;
}

/// A mute changed: the verdict of the repository from its stored facts, without the muted rules.
void recompute_attention(const std::string& user, const std::string& full_name) {
    GithubRepos::RepoRepository repos;
    const auto facts = repos.facts(user, full_name);
    if (!facts) {
        return;
    }
    repos.set_attention(user,
                        full_name,
                        ::Github::attention(*facts,
                                            GithubRepos::MuteRepository().of(user, full_name),
                                            static_cast<long long>(Utils::Time::now_epoch_seconds())));
}

/// The row of the repository after a mute changed, or 404 github_repo_not_found.
void answer_row(const std::string& user,
                const std::string& full_name,
                const std::function<void(const HttpResponsePtr&)>& callback) {
    const auto row = GithubRepos::RepoRepository().one(user, full_name);
    if (!row) {
        callback(ErrorResponse::not_found("github_repo_not_found"));
        return;
    }
    callback(Response::ok(json{{"data", *row}}));
}

/// What the task of @p rule is about: the verdict's detail when the rule is in it, else the rule's own words.
std::string task_reason(const json& row, const std::string& rule) {
    if (row.contains("attention") && row["attention"].is_array()) {
        for (const auto& item : row["attention"]) {
            if (item.value("rule", "") == rule && item.contains("detail") && item["detail"].is_string()) {
                return item["detail"].get<std::string>();
            }
        }
    }
    if (rule == ::Github::kRuleChecks) {
        return "failing checks";
    }
    if (rule == ::Github::kRuleAlerts) {
        return "Dependabot alerts";
    }
    if (rule == ::Github::kRulePrs) {
        return "open pull requests";
    }
    if (rule == ::Github::kRuleIssues) {
        return "open issues";
    }
    if (rule == ::Github::kRuleHygiene) {
        return "description, license or README";
    }
    return "archive it?";
}

}  // namespace

void GithubController::repos(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, owner);
    GithubRepos::RepoRepository::Filter f;
    if (!req->getParameter("view").empty()) {
        f.view = req->getParameter("view");
    }
    if (std::find(kViews.begin(), kViews.end(), f.view) == kViews.end()) {
        callback(ErrorResponse::bad_request("invalid_query", "view is attention, all or archived"));
        return;
    }
    f.owner = req->getParameter("owner");
    f.q = req->getParameter("q");
    const auto page = parse_page_params(req, kDefaultLimit, kMaxLimit);
    f.limit = page.limit;
    f.offset = page.offset;
    with_repo_errors(callback, "github.repos", [&] {
        if (!require_link(owner, callback)) {
            return;
        }
        json out = GithubRepos::RepoRepository().list(owner, f);
        out["limit"] = f.limit;
        out["offset"] = f.offset;
        callback(Response::ok(out));
    });
}

void GithubController::repo(const HttpRequestPtr& req,
                            Callback&& callback,
                            const std::string& owner,
                            const std::string& name) {
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, user);
    with_repo_errors(callback, "github.repo", [&] {
        if (!require_link(user, callback)) {
            return;
        }
        const auto row = GithubRepos::RepoRepository().one(user, owner + "/" + name);
        if (!row) {
            callback(ErrorResponse::not_found("github_repo_not_found"));
            return;
        }
        callback(Response::ok(json{{"data", *row}}));
    });
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
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, user);
    API_REFUSE_API_KEY(req, callback);
    if (!::Github::is_rule(rule)) {
        callback(ErrorResponse::bad_request("invalid_rule", "rule is checks, alerts, prs, issues, hygiene or stale"));
        return;
    }
    const std::string full_name = owner + "/" + name;
    with_repo_errors(callback, "github.mute", [&] {
        if (!require_link(user, callback)) {
            return;
        }
        if (!GithubRepos::RepoRepository().facts(user, full_name)) {
            callback(ErrorResponse::not_found("github_repo_not_found"));
            return;
        }
        GithubRepos::MuteRepository().put(user, full_name, rule);
        recompute_attention(user, full_name);
        answer_row(user, full_name, callback);
    });
}

void GithubController::unmute(const HttpRequestPtr& req,
                              Callback&& callback,
                              const std::string& owner,
                              const std::string& name,
                              const std::string& rule) {
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, user);
    API_REFUSE_API_KEY(req, callback);
    if (!::Github::is_rule(rule)) {
        callback(ErrorResponse::bad_request("invalid_rule", "rule is checks, alerts, prs, issues, hygiene or stale"));
        return;
    }
    const std::string full_name = owner + "/" + name;
    with_repo_errors(callback, "github.unmute", [&] {
        if (!require_link(user, callback)) {
            return;
        }
        if (!GithubRepos::RepoRepository().facts(user, full_name)) {
            callback(ErrorResponse::not_found("github_repo_not_found"));
            return;
        }
        GithubRepos::MuteRepository().remove(user, full_name, rule);
        recompute_attention(user, full_name);
        answer_row(user, full_name, callback);
    });
}

void GithubController::makeTask(const HttpRequestPtr& req,
                                Callback&& callback,
                                const std::string& owner,
                                const std::string& name) {
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, user);
    API_REFUSE_API_KEY(req, callback);
    API_REQUIRE_MODULE(Core::tasks_enabled(), "tasks", callback);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const std::string rule = body.contains("rule") && body["rule"].is_string() ? body["rule"].get<std::string>() : "";
    if (!::Github::is_rule(rule)) {
        callback(ErrorResponse::bad_request("invalid_rule", "rule is checks, alerts, prs, issues, hygiene or stale"));
        return;
    }
    const std::string full_name = owner + "/" + name;
    with_repo_errors(callback, "github.makeTask", [&] {
        if (!require_link(user, callback)) {
            return;
        }
        GithubRepos::RepoRepository repos;
        const auto row = repos.one(user, full_name);
        if (!row) {
            callback(ErrorResponse::not_found("github_repo_not_found"));
            return;
        }
        Repositories::Tasks::TaskRepository tasks;
        // One task per repository and rule: the one made before, while it is still open.
        if (const auto existing = repos.task_of(user, full_name, rule)) {
            const auto task = tasks.get(user, *existing);
            if (task && (*task)["status"].get<std::string>() != "done") {
                callback(Response::created(json{{"data", json{{"task_id", *existing}, {"created", false}}}}));
                return;
            }
            repos.clear_task(user, full_name, rule);
        }
        const std::string url = "https://github.com/" + full_name;
        Repositories::Tasks::TaskRepository::Input in;
        in.title = (full_name + ": " + task_reason(*row, rule)).substr(0, ::Tasks::Fields::kTitleMax);
        in.area = "projects";
        in.note = url + "\nGitHub rule: " + rule;
        in.source_kind = "url";
        in.source_ref = url;
        const json task = tasks.create(user, in);
        const std::string task_id = task["id"].get<std::string>();
        repos.set_task(user, full_name, rule, task_id);
        callback(Response::created(json{{"data", json{{"task_id", task_id}, {"created", true}}}}));
    });
}

}  // namespace Api
