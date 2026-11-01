/**
 * @file GithubController.hpp
 * @brief The caller's GitHub (github/): link through GitHub's OAuth App (the
 *        callback is public and finds the user by a one-time state),
 *        unlink, the repositories as the last sync saw them with what needs
 *        attention, "Sync now", "Not about this" per rule, and a task made
 *        from an attention item. The contract is docs/openapi.yaml (tag
 *        github); every handler answers 501 not_implemented until its body
 *        lands.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class GithubController : public HttpController<GithubController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(GithubController::status, "/api/v1/github", Get);
    ADD_METHOD_TO(GithubController::oauthStart, "/api/v1/github/oauth/start", Get);
    ADD_METHOD_TO(GithubController::oauthCallback, "/api/v1/github/oauth/callback", Get);
    ADD_METHOD_TO(GithubController::disconnect, "/api/v1/github", Delete);
    ADD_METHOD_TO(GithubController::repos, "/api/v1/github/repos", Get);
    ADD_METHOD_TO(GithubController::repo, "/api/v1/github/repos/{owner}/{name}", Get);
    ADD_METHOD_TO(GithubController::sync, "/api/v1/github/sync", Post);
    ADD_METHOD_TO(GithubController::mute, "/api/v1/github/repos/{owner}/{name}/mutes/{rule}", Put);
    ADD_METHOD_TO(GithubController::unmute, "/api/v1/github/repos/{owner}/{name}/mutes/{rule}", Delete);
    ADD_METHOD_TO(GithubController::makeTask, "/api/v1/github/repos/{owner}/{name}/task", Post);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void status(const HttpRequestPtr& req, Callback&& callback);
    void oauthStart(const HttpRequestPtr& req, Callback&& callback);
    void oauthCallback(const HttpRequestPtr& req, Callback&& callback);
    void disconnect(const HttpRequestPtr& req, Callback&& callback);
    void repos(const HttpRequestPtr& req, Callback&& callback);
    void repo(const HttpRequestPtr& req, Callback&& callback, const std::string& owner, const std::string& name);
    void sync(const HttpRequestPtr& req, Callback&& callback);
    void mute(const HttpRequestPtr& req,
              Callback&& callback,
              const std::string& owner,
              const std::string& name,
              const std::string& rule);
    void unmute(const HttpRequestPtr& req,
                Callback&& callback,
                const std::string& owner,
                const std::string& name,
                const std::string& rule);
    void makeTask(const HttpRequestPtr& req, Callback&& callback, const std::string& owner, const std::string& name);
};

}  // namespace Api
