/**
 * @file AniHookController.hpp
 * @brief Ani's personal webhook (agent/Hook.hpp) and the inbox the Now page
 *        plays from. The ingest routes are public: the token in the path or
 *        in X-Ani-Hook-Token is the credential, and an unknown token and a
 *        replaced one get the same 404. Each call counts against 120 an
 *        hour per user; at most 50 items wait. say, outfit and act wait
 *        ready; an event waits thinking while the worker hands it to the
 *        user's agent (job ani_hook_event). The claim and count routes are
 *        for the signed-in user's page (a browser session only).
 *        Logs never carry the token (Hook::redact).
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class AniHookController : public HttpController<AniHookController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AniHookController::ingest, "/api/v1/ani/hook/{token}", Post);
    ADD_METHOD_TO(AniHookController::ingestHeader, "/api/v1/ani/hook", Post);
    ADD_METHOD_TO(AniHookController::claim, "/api/v1/ani/inbox/claim", Post);
    ADD_METHOD_TO(AniHookController::count, "/api/v1/ani/inbox/count", Get);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    /// Items one claim hands out.
    static constexpr int kClaimMax = 5;

    void ingest(const HttpRequestPtr& req, Callback&& callback, const std::string& token);
    void ingestHeader(const HttpRequestPtr& req, Callback&& callback);
    void claim(const HttpRequestPtr& req, Callback&& callback);
    void count(const HttpRequestPtr& req, Callback&& callback);
};

}  // namespace Api
