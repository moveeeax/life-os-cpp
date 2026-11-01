/**
 * @file GithubController.cpp
 * @brief Body for src/api/GithubController.hpp. The routes and guards are in
 *        place; each handler answers 501 not_implemented until the API lane
 *        fills it in against docs/openapi.yaml (tag github),
 *        repositories/github/*.hpp and github/Rules.hpp.
 */

#include "api/GithubController.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <drogon/drogon.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "github/Rules.hpp"
#include "repositories/github/LinkRepository.hpp"
#include "repositories/github/MuteRepository.hpp"
#include "repositories/github/RepoRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "tasks/Fields.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"
#include "utils/Time.hpp"

namespace Api {

namespace {

/// The stub answer of every handler below.
drogon::HttpResponsePtr not_implemented() {
    return ErrorResponse::make({drogon::k501NotImplemented, "not_implemented", "GitHub is not built yet"});
}

}  // namespace

void GithubController::status(const HttpRequestPtr& req, Callback&& callback) {
    // TODO(github api): {configured: Github::settings().has_value(), ...LinkRepository().status(owner) or
    // {linked: false, login: null, scopes: [], linked_at: null, synced_at: null, sync_error: null, org_hint: null}}
    // as data (GithubStatus). Read keys allowed.
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, owner);
    (void)owner;
    callback(not_implemented());
}

void GithubController::oauthStart(const HttpRequestPtr& req, Callback&& callback) {
    // TODO(github api): refuse API keys; 404 github_not_configured without Github::settings(); a one-time state
    // (LinkRepository::new_state) and a 302 to Github::kAuthorizeUrl with client_id, redirect_uri, scope
    // (Github::kScope) and state.
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, owner);
    (void)owner;
    API_REFUSE_API_KEY(req, callback);
    callback(not_implemented());
}

void GithubController::oauthCallback(const HttpRequestPtr& req, Callback&& callback) {
    // TODO(github api): public (api.public_paths). take_state(state, session of the access cookie) names the user;
    // exchange the code at Github::kTokenUrl, read the login (GET /user) and the granted scopes, seal the token
    // (Utils::Seal with settings().token_key), LinkRepository::set, queue one github_sync for the user, audit
    // github.link, and redirect to /account?github=ok | denied | expired | error. Never answer JSON here.
    (void)req;
    if (!Core::github_enabled()) {
        callback(drogon::HttpResponse::newRedirectionResponse("/account?github=error"));
        return;
    }
    callback(drogon::HttpResponse::newRedirectionResponse("/account?github=error"));
}

void GithubController::disconnect(const HttpRequestPtr& req, Callback&& callback) {
    // TODO(github api): refuse API keys; 404 github_not_linked without a link; revoke the token at GitHub (best
    // effort, DELETE /applications/{client_id}/token), LinkRepository::remove (repositories and mutes go with it),
    // audit github.unlink, 204.
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, owner);
    (void)owner;
    API_REFUSE_API_KEY(req, callback);
    callback(not_implemented());
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
    // TODO(github api): refuse API keys; 404 github_not_linked without a link; 429 sync_too_soon with
    // retry_after_sec when LinkRepository::seconds_since_sync < Github::kSyncMinSeconds (a queued but unfinished
    // sync counts too: keep a Redis key github:sync:<owner> for a minute); Jobs submit Github::kSyncJob
    // {owner_id}; 202 {data: {status: queued}}.
    API_MODULE_USER_GUARD(Core::github_enabled(), "github", req, callback, owner);
    (void)owner;
    API_REFUSE_API_KEY(req, callback);
    callback(not_implemented());
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
