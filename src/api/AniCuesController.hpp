/**
 * @file AniCuesController.hpp
 * @brief The /api/v1/ani/cues routes: Ani's cues for the screens of the app
 *        on a local day, computed by the rules of agent/Cues.hpp from the
 *        person's food diary, calendar, sleep, tape and month of money, then
 *        the cue her agent wrote itself (agent/CueTools.hpp); and sending a
 *        cue away until the next local day. Works with the agent off; a
 *        browser session only (an API key gets 401).
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class AniCuesController : public HttpController<AniCuesController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AniCuesController::list, "/api/v1/ani/cues", Get);
    ADD_METHOD_TO(AniCuesController::dismiss, "/api/v1/ani/cues/{id}/dismiss", Post);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void list(const HttpRequestPtr& req, Callback&& callback);
    void dismiss(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
};

}  // namespace Api
