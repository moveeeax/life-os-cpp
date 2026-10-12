/**
 * @file WallpapersController.hpp
 * @brief The wallpaper library: the admin uploads a video or an image straight
 *        to the bucket through presigned URLs and marks it ready once both
 *        objects are there; every signed-in user lists the ready ones of a
 *        device. 404 while the now module is off, 503 while storage is not
 *        configured.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class WallpapersController : public HttpController<WallpapersController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(WallpapersController::listWallpapers, "/api/v1/wallpapers", Get);
    ADD_METHOD_TO(WallpapersController::listAdmin, "/api/v1/admin/wallpapers", Get);
    ADD_METHOD_TO(WallpapersController::createWallpaper, "/api/v1/admin/wallpapers", Post);
    ADD_METHOD_TO(WallpapersController::markReady, "/api/v1/admin/wallpapers/{id}/ready", Post);
    ADD_METHOD_TO(WallpapersController::deleteWallpaper, "/api/v1/admin/wallpapers/{id}", Delete);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void listWallpapers(const HttpRequestPtr& req, Callback&& callback);
    void listAdmin(const HttpRequestPtr& req, Callback&& callback);
    void createWallpaper(const HttpRequestPtr& req, Callback&& callback);
    void markReady(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteWallpaper(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
};

}  // namespace Api
