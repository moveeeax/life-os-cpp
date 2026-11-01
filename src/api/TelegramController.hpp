/**
 * @file TelegramController.hpp
 * @brief The owner's Telegram as Ani and the owner reach it (telegram/). An
 *        admin links the gateway account to themselves (one user at most);
 *        every other route serves only that user while they stay an admin,
 *        and answers 404 to anyone else. No route takes an API key. Reading, reactions and "read" go to the gateway at
 * once; sending and editing are what Ani's confirmed proposals call (the owner may call them too). The gateway token
 * stays on the server.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class TelegramController : public HttpController<TelegramController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TelegramController::status, "/api/v1/telegram", Get);
    ADD_METHOD_TO(TelegramController::link, "/api/v1/telegram", Put);
    ADD_METHOD_TO(TelegramController::unlink, "/api/v1/telegram", Delete);
    ADD_METHOD_TO(TelegramController::chats, "/api/v1/telegram/chats", Get);
    ADD_METHOD_TO(TelegramController::chat, "/api/v1/telegram/chats/{id}", Get);
    ADD_METHOD_TO(TelegramController::messages, "/api/v1/telegram/chats/{id}/messages", Get);
    ADD_METHOD_TO(TelegramController::send, "/api/v1/telegram/chats/{id}/messages", Post);
    ADD_METHOD_TO(TelegramController::edit, "/api/v1/telegram/chats/{id}/messages/{mid}", Patch);
    ADD_METHOD_TO(TelegramController::react, "/api/v1/telegram/chats/{id}/messages/{mid}/reactions", Post);
    ADD_METHOD_TO(TelegramController::read, "/api/v1/telegram/chats/{id}/read", Post);
    ADD_METHOD_TO(TelegramController::search, "/api/v1/telegram/search", Get);
    ADD_METHOD_TO(TelegramController::waiting, "/api/v1/telegram/waiting", Get);
    ADD_METHOD_TO(TelegramController::quietList, "/api/v1/telegram/quiet", Get);
    ADD_METHOD_TO(TelegramController::quietOn, "/api/v1/telegram/quiet/{id}", Put);
    ADD_METHOD_TO(TelegramController::quietOff, "/api/v1/telegram/quiet/{id}", Delete);
    ADD_METHOD_TO(TelegramController::settings, "/api/v1/telegram/settings", Put);
    ADD_METHOD_TO(TelegramController::toSelf, "/api/v1/telegram/self", Post);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void status(const HttpRequestPtr& req, Callback&& callback);
    void link(const HttpRequestPtr& req, Callback&& callback);
    void unlink(const HttpRequestPtr& req, Callback&& callback);
    void chats(const HttpRequestPtr& req, Callback&& callback);
    void chat(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void messages(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void send(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void edit(const HttpRequestPtr& req, Callback&& callback, const std::string& id, const std::string& mid);
    void react(const HttpRequestPtr& req, Callback&& callback, const std::string& id, const std::string& mid);
    void read(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void search(const HttpRequestPtr& req, Callback&& callback);
    void waiting(const HttpRequestPtr& req, Callback&& callback);
    void quietList(const HttpRequestPtr& req, Callback&& callback);
    void quietOn(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void quietOff(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void settings(const HttpRequestPtr& req, Callback&& callback);
    void toSelf(const HttpRequestPtr& req, Callback&& callback);
};

}  // namespace Api
