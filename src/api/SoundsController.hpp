/**
 * @file SoundsController.hpp
 * @brief The focus sound library: the admin uploads a sound straight to the
 *        bucket through a presigned URL and marks it ready once it is there;
 *        every signed-in user lists the ready ones. 404 while the now module
 *        is off, 503 while storage is not configured.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class SoundsController : public HttpController<SoundsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SoundsController::listSounds, "/api/v1/sounds", Get);
    ADD_METHOD_TO(SoundsController::listAdmin, "/api/v1/admin/sounds", Get);
    ADD_METHOD_TO(SoundsController::createSound, "/api/v1/admin/sounds", Post);
    ADD_METHOD_TO(SoundsController::markReady, "/api/v1/admin/sounds/{id}/ready", Post);
    ADD_METHOD_TO(SoundsController::deleteSound, "/api/v1/admin/sounds/{id}", Delete);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void listSounds(const HttpRequestPtr& req, Callback&& callback);
    void listAdmin(const HttpRequestPtr& req, Callback&& callback);
    void createSound(const HttpRequestPtr& req, Callback&& callback);
    void markReady(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteSound(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
};

}  // namespace Api
