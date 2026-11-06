/**
 * @file AniSettingsController.hpp
 * @brief Ani's settings kept on the server, for the signed-in user: the kinds
 *        of her lines muted (GET /ani/bare answers 410 since the bare outfit
 *        was removed; the route goes in a later release); the user's webhook URL (made on first read, replaceable) with
 * the last calls. Works with the agent off; a browser session only (an API key gets 401).
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
    ADD_METHOD_TO(AniSettingsController::facts, "/api/v1/ani/facts", Get);
    ADD_METHOD_TO(AniSettingsController::addFact, "/api/v1/ani/facts", Post);
    ADD_METHOD_TO(AniSettingsController::editFact, "/api/v1/ani/facts/{id}", Patch);
    ADD_METHOD_TO(AniSettingsController::removeFact, "/api/v1/ani/facts/{id}", Delete);
    ADD_METHOD_TO(AniSettingsController::deals, "/api/v1/ani/deals", Get);
    ADD_METHOD_TO(AniSettingsController::bare, "/api/v1/ani/bare", Get);
    ADD_METHOD_TO(AniSettingsController::removeDeal, "/api/v1/ani/deals/{id}", Delete);
    ADD_METHOD_TO(AniSettingsController::webhook, "/api/v1/ani/webhook", Get);
    ADD_METHOD_TO(AniSettingsController::rotateWebhook, "/api/v1/ani/webhook/rotate", Post);
    ADD_METHOD_TO(AniSettingsController::reminders, "/api/v1/ani/reminders", Get);
    ADD_METHOD_TO(AniSettingsController::addReminder, "/api/v1/ani/reminders", Post);
    ADD_METHOD_TO(AniSettingsController::removeReminder, "/api/v1/ani/reminders/{id}", Delete);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void prefs(const HttpRequestPtr& req, Callback&& callback);
    /// 410 bare_removed: kept only until a later release drops the route from the spec.
    void bare(const HttpRequestPtr& req, Callback&& callback);
    /// Her recurring reminders (agent/Reminders.hpp): list, add (at most 20), remove.
    void reminders(const HttpRequestPtr& req, Callback&& callback);
    void addReminder(const HttpRequestPtr& req, Callback&& callback);
    void removeReminder(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void savePrefs(const HttpRequestPtr& req, Callback&& callback);
    /// This week's promises to Ani with their progress, and dropping one.
    void deals(const HttpRequestPtr& req, Callback&& callback);
    void removeDeal(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void facts(const HttpRequestPtr& req, Callback&& callback);
    void addFact(const HttpRequestPtr& req, Callback&& callback);
    void editFact(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void removeFact(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void webhook(const HttpRequestPtr& req, Callback&& callback);
    void rotateWebhook(const HttpRequestPtr& req, Callback&& callback);
};

}  // namespace Api
