/**
 * @file AniController.hpp
 * @brief The /api/v1/ani/agent routes: the state of the caller's agent pod
 *        (agent/Lifecycle.hpp), waking it and stopping it. 404 while the agent
 *        is switched off. Administrators only until the chat reaches everyone.
 */

#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class AniController : public HttpController<AniController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AniController::agent, "/api/v1/ani/agent", Get);
    ADD_METHOD_TO(AniController::wake, "/api/v1/ani/agent/wake", Post);
    ADD_METHOD_TO(AniController::stop, "/api/v1/ani/agent/stop", Post);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void agent(const HttpRequestPtr& req, Callback&& callback);
    void wake(const HttpRequestPtr& req, Callback&& callback);
    void stop(const HttpRequestPtr& req, Callback&& callback);
};

}  // namespace Api
