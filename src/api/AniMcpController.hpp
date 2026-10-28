/**
 * @file AniMcpController.hpp
 * @brief Ani's MCP endpoint (Streamable HTTP, src/mcp): one JSON-RPC request
 *        per POST, answered with plain JSON; GET is 405 (no server stream).
 *        Signed in with an API key of scope "ani" (or "full"; a "read" key
 *        is refused) in the Authorization header, or a browser session. A
 *        request from a page of another site (Origin) is refused 403.
 *        Request bodies are never logged.
 */

#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class AniMcpController : public HttpController<AniMcpController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AniMcpController::post, "/api/v1/ani/mcp", Post);
    ADD_METHOD_TO(AniMcpController::get, "/api/v1/ani/mcp", Get);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void post(const HttpRequestPtr& req, Callback&& callback);
    void get(const HttpRequestPtr& req, Callback&& callback);
};

}  // namespace Api
