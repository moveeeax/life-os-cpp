/**
 * @file AniSettingsController.hpp
 * @brief Ani's settings kept on the server, for the signed-in user: the bare
 *        outfit switch (on only with "adult": true) and the two short signed
 *        links its model loads from the private part of the bucket; the
 *        user's webhook URL (made on first read, replaceable) with the last
 *        calls. Works with the agent off; a browser session only (an API
 *        key gets 401).
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class AniSettingsController : public HttpController<AniSettingsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AniSettingsController::prefs, "/api/v1/ani/prefs", Get);
    ADD_METHOD_TO(AniSettingsController::savePrefs, "/api/v1/ani/prefs", Put);
    ADD_METHOD_TO(AniSettingsController::bare, "/api/v1/ani/bare", Get);
    ADD_METHOD_TO(AniSettingsController::webhook, "/api/v1/ani/webhook", Get);
    ADD_METHOD_TO(AniSettingsController::rotateWebhook, "/api/v1/ani/webhook/rotate", Post);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    /// The bare model's links live this long.
    static constexpr long kLinkSeconds = 120;
    /// Where the bare model lives: <prefix>private/ani/bare-v1/. New files get a new folder.
    static constexpr const char* kBareFolder = "bare-v1";

    void prefs(const HttpRequestPtr& req, Callback&& callback);
    void savePrefs(const HttpRequestPtr& req, Callback&& callback);
    void bare(const HttpRequestPtr& req, Callback&& callback);
    void webhook(const HttpRequestPtr& req, Callback&& callback);
    void rotateWebhook(const HttpRequestPtr& req, Callback&& callback);
};

}  // namespace Api
