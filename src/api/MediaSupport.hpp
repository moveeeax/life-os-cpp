/**
 * @file MediaSupport.hpp
 * @brief The media-upload flow the Sounds and Wallpapers controllers share:
 *        the module and store guard, the upload descriptor, the size check of
 *        an uploaded object, and the delete that takes the row out of the
 *        players before the objects go. Each controller keeps its own
 *        validation and public row.
 */

#pragma once

#include <exception>
#include <functional>
#include <string>
#include <vector>

#include <drogon/HttpResponse.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/HandlerSupport.hpp"
#include "storage/ObjectStore.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

namespace Api::Media {

using Callback = std::function<void(const drogon::HttpResponsePtr&)>;

/// The Now module switch, then the store; nullptr (answered) when either is
/// missing. @p what names the media in the 503 ("sound", "wallpaper"). It runs
/// before the auth guards, so a missing store answers 503 before a 401.
inline Storage::ObjectStore* store_or_answer(const Callback& callback, const std::string& what) {
    if (!Core::now_enabled()) {
        callback(ErrorResponse::not_found("now"));
        return nullptr;
    }
    Storage::ObjectStore* store = Storage::object_store();
    if (store == nullptr) {
        callback(ErrorResponse::service_unavailable("storage_not_configured",
                                                    "the " + what + " storage is not configured (docs/wallpapers.md)"));
    }
    return store;
}

/// Where and how the browser PUTs one object.
inline nlohmann::json upload_of(const std::string& url, const std::string& content_type) {
    return nlohmann::json{
        {"url", url},
        {"headers", {{"Content-Type", content_type}, {"Cache-Control", Storage::kImmutableCacheControl}}}};
}

/// Answer 503 storage_error after the store failed; @p op labels the log line.
inline void storage_error(const Callback& callback, const char* op, const std::exception& e) {
    spdlog::warn("{}: the store did not answer: {}", op, e.what());
    callback(ErrorResponse::service_unavailable("storage_error", "the storage did not answer; try again"));
}

/// Whether the object at @p key is in the bucket at @p bytes. Throws when the store does not answer.
inline bool object_matches(Storage::ObjectStore& store, const std::string& key, long long bytes) {
    const auto info = store.head(key);
    return info.exists && info.bytes == bytes;
}

/**
 * @brief Delete a media row and its objects, answering 204. The row leaves the
 *        players first (mark_pending): if the store or the final DELETE fails,
 *        a pending row stays behind for the media sweep, never a ready row
 *        pointing at a gone object. A store failure answers 503 storage_error.
 */
template <class Repo>
void remove_then_delete(Repo& repo,
                        Storage::ObjectStore& store,
                        const std::string& id,
                        const std::vector<std::string>& keys,
                        const Callback& callback,
                        const char* op) {
    repo.mark_pending(id);
    try {
        for (const auto& key : keys) {
            store.remove(key);
        }
    } catch (const std::exception& e) {
        storage_error(callback, op, e);
        return;
    }
    repo.remove(id);
    no_content(callback);
}

}  // namespace Api::Media
