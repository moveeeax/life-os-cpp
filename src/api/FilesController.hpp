/**
 * @file FilesController.hpp
 * @brief Private file attachments of a medical record or a travel document.
 *        The browser PUTs the bytes straight to the bucket under the private
 *        prefix through a presigned URL, the API checks the object and marks
 *        the row ready, and a read is a presigned GET valid two minutes,
 *        made for the owner on every request; nothing here is ever served
 *        from the public base URL. Every route is owner-scoped: another
 *        owner's file or parent answers 404. 404 while the parent's module
 *        (medical, travel) is off, 503 while storage is not configured or
 *        refuses.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class FilesController : public HttpController<FilesController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(FilesController::listFiles, "/api/v1/files", Get);
    ADD_METHOD_TO(FilesController::createFile, "/api/v1/files", Post);
    ADD_METHOD_TO(FilesController::markReady, "/api/v1/files/{id}/ready", Post);
    ADD_METHOD_TO(FilesController::downloadFile, "/api/v1/files/{id}/download", Get);
    ADD_METHOD_TO(FilesController::deleteFile, "/api/v1/files/{id}", Delete);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void listFiles(const HttpRequestPtr& req, Callback&& callback);
    void createFile(const HttpRequestPtr& req, Callback&& callback);
    void markReady(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void downloadFile(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteFile(const HttpRequestPtr& req, Callback&& callback, const std::string& id);

    /// Seconds a download link stays valid.
    static constexpr long kDownloadSeconds = 120;
    /// The largest file, in bytes.
    static constexpr long long kMaxBytes = 25LL * 1024LL * 1024LL;
};

}  // namespace Api
