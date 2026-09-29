/**
 * @file UploadController.hpp
 * @brief Admin image upload: POST /api/v1/admin/uploads (multipart) stores the
 *        file in the configured Storage backend (local or S3/MinIO) under a
 *        random key and returns its public URL. The admin post editor inserts
 *        that URL into the Markdown body as an image.
 *
 *        With the local backend that URL is same-origin (/uploads/<key>) and
 *        this controller also SERVES it — see serveUpload. With an S3/CDN
 *        origin configured (storage.public_base_url) the URL is absolute and
 *        the read route is dead weight: it 404s.
 *
 *        Every handler is gated by Core::content_enabled() first — same
 *        module-off contract as PostsController/ContentPagesController: with
 *        the content module disabled, every route here (including the public
 *        read route) 404s instead of touching Storage.
 *
 *        Declarations only — the handler bodies live in UploadController.cpp
 *        (compiled once into app_core; ADR 0003 as amended 2026-08-22). The
 *        route macros (ADD_METHOD_TO) must stay in this header: Drogon's
 *        METHOD_LIST registration is part of the class definition, and
 *        scripts/check-routes-registered.sh greps the src/api headers for
 *        them.
 */

#pragma once

#include <functional>
#include <string>
#include <string_view>

#include <drogon/HttpController.h>

#include <nlohmann/json_fwd.hpp>

namespace Api {

using namespace drogon;
using json = nlohmann::json;

// Cheap magic-number sniff: confirm the bytes actually match the claimed image
// type, so a .jpg that's really HTML/script can't be stored and served back.
bool image_bytes_match(const std::string& ext, std::string_view b);

class UploadController : public HttpController<UploadController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UploadController::upload, "/api/v1/admin/uploads", Post);
    // Media library: list + delete what the editor uploaded.
    ADD_METHOD_TO(UploadController::listUploads, "/api/v1/admin/uploads", Get);
    ADD_METHOD_TO(UploadController::deleteUpload, "/api/v1/admin/uploads/{1}", Delete);
    // Public read of a stored object (local backend only) — this is the URL
    // Storage::url() hands the editor when no CDN origin is configured. Via
    // regex because keys contain a '/' (posts/<hex>.<ext>) and a {1} path
    // parameter only ever matches a single segment.
    ADD_METHOD_VIA_REGEX(UploadController::serveUpload, "/uploads/(.*)", Get);
    METHOD_LIST_END

    void upload(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);

    // GET /api/v1/admin/uploads — offset-paged listing of the posts/ prefix.
    void listUploads(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);

    // DELETE /api/v1/admin/uploads/{name} — name is the single-segment
    // basename of an upload key (keys are posts/<hex>.<ext>).
    void deleteUpload(const HttpRequestPtr& req,
                      std::function<void(const HttpResponsePtr&)>&& callback,
                      const std::string& name);

    // GET /uploads/{key} — read an uploaded image back out of local storage.
    // Public (post bodies link to it) and read-only. Everything that isn't a
    // safe key naming an object with an allowlisted raster extension is a 404,
    // including every request in S3/CDN mode: there storage.public_base_url is
    // set, the stored URLs point at that origin, and nothing links here.
    void serveUpload(const HttpRequestPtr&,
                     std::function<void(const HttpResponsePtr&)>&& callback,
                     const std::string& key);

private:
    // True when this process is the origin for uploads: the local backend with
    // no external public base URL, which is exactly when Storage::url() returns
    // the same-origin /uploads/<key> path that serveUpload answers.
    static bool serves_uploads_locally();

    // Reject with 503 unless a Storage backend is configured. Returns false
    // after responding — callers `if (!require_storage(callback)) return;`,
    // same contract as the Api::require_* guards.
    static bool require_storage(const std::function<void(const HttpResponsePtr&)>& callback);

    // The single extension → MIME table (uploads are raster images only; SVG
    // intentionally excluded: it can carry inline <script> → stored XSS when
    // served same-origin). Lowercases its input; empty result = unknown ext.
    static std::string mime_for_ext(std::string ext);

    // Extension → content type for listings/serving; unknown extensions map to
    // application/octet-stream (which serveUpload treats as "not servable").
    static std::string content_type_for(const std::string& name);
};

}  // namespace Api
