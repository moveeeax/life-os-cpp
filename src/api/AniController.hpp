/**
 * @file AniController.hpp
 * @brief The /api/v1/ani routes: the state of the caller's agent pod
 *        (agent/Lifecycle.hpp), waking and stopping it, and the chat with it
 *        (agent/Turn.hpp): a message queues a turn, the page polls the turn
 *        for the reply. 404 while the agent is switched off; a browser session
 *        only (an API key gets 401).
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class AniController : public HttpController<AniController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AniController::agent, "/api/v1/ani/agent", Get);
    ADD_METHOD_TO(AniController::wake, "/api/v1/ani/agent/wake", Post);
    ADD_METHOD_TO(AniController::stop, "/api/v1/ani/agent/stop", Post);
    ADD_METHOD_TO(AniController::message, "/api/v1/ani/messages", Post);
    ADD_METHOD_TO(AniController::turns, "/api/v1/ani/turns", Get);
    ADD_METHOD_TO(AniController::turn, "/api/v1/ani/turns/{id}", Get);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void agent(const HttpRequestPtr& req, Callback&& callback);
    void wake(const HttpRequestPtr& req, Callback&& callback);
    void stop(const HttpRequestPtr& req, Callback&& callback);
    void message(const HttpRequestPtr& req, Callback&& callback);
    void turns(const HttpRequestPtr& req, Callback&& callback);
    void turn(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
};

}  // namespace Api
