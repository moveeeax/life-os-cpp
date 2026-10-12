/**
 * @file WallpapersController.cpp
 * @brief The /api/v1/wallpapers and /api/v1/admin/wallpapers handlers
 *        (docs/superpowers/specs/2026-10-12-now-shell-design.md §1).
 */

#include "api/WallpapersController.hpp"

#include <exception>
#include <optional>
#include <string>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "api/Validation.hpp"
#include "core/Modules.hpp"
#include "repositories/now/WallpaperRepository.hpp"
#include "storage/ObjectStore.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Repositories::Now::WallpaperRepository;

constexpr long long kMb = 1024LL * 1024LL;
constexpr long long kVideoMax = 100 * kMb;
constexpr long long kImageMax = 10 * kMb;
constexpr long long kPosterMax = kMb;
constexpr int kSideMax = 10000;

const std::vector<std::string> kKinds = {"video", "image"};
const std::vector<std::string> kDevices = {"desktop", "phone"};
const std::vector<std::string> kVideoTypes = {"video/mp4", "video/webm"};
const std::vector<std::string> kImageTypes = {"image/jpeg", "image/webp"};

/// The module switch, then the store; nullptr (answered) when either is missing.
Storage::ObjectStore* store_or_answer(const WallpapersController::Callback& callback) {
    if (!Core::now_enabled()) {
        callback(ErrorResponse::not_found("now"));
        return nullptr;
    }
    Storage::ObjectStore* store = Storage::object_store();
    if (store == nullptr) {
        callback(ErrorResponse::service_unavailable("storage_not_configured",
                                                    "the wallpaper storage is not configured (docs/wallpapers.md)"));
    }
    return store;
}

std::string extension_of(const std::string& content_type) {
    if (content_type == "video/mp4") {
        return "mp4";
    }
    if (content_type == "video/webm") {
        return "webm";
    }
    if (content_type == "image/webp") {
        return "webp";
    }
    return "jpg";
}

/// A whole number within [1, max]; adds an error otherwise. Absent is the caller's business.
void size_field(Validation::Errors& errs, const json& body, const char* field, long long max) {
    if (!body.contains(field) || body[field].is_null()) {
        return;
    }
    if (!body[field].is_number_integer() || body[field].get<long long>() < 1) {
        errs.add(field, "invalid", std::string(field) + " must be a positive whole number of bytes");
    } else if (body[field].get<long long>() > max) {
        errs.add(field, "too_large", std::string(field) + " must be at most " + std::to_string(max) + " bytes");
    }
}

std::optional<int> opt_side(const json& body, const char* field) {
    if (body.contains(field) && body[field].is_number_integer()) {
        return body[field].get<int>();
    }
    return std::nullopt;
}

/// The public shape of a ready row: where the browser reads it.
json public_row(const Storage::ObjectStore& store, const json& row) {
    return json{{"id", row["id"]},
                {"kind", row["kind"]},
                {"url", store.public_url(row["object_key"].get<std::string>())},
                {"poster_url",
                 row["poster_key"].is_string() ? json(store.public_url(row["poster_key"].get<std::string>())) : json()},
                {"width", row["width"]},
                {"height", row["height"]}};
}

json upload_of(const std::string& url, const std::string& content_type) {
    return json{{"url", url},
                {"headers", {{"Content-Type", content_type}, {"Cache-Control", Storage::kImmutableCacheControl}}}};
}

}  // namespace

void WallpapersController::listWallpapers(const HttpRequestPtr& req, Callback&& callback) {
    Storage::ObjectStore* store = store_or_answer(callback);
    if (store == nullptr) {
        return;
    }
    API_REQUIRE_OWNER(req, callback, owner);
    const std::string device = req->getParameter("device");
    if (device != "desktop" && device != "phone") {
        callback(ErrorResponse::bad_request("invalid_query", "device must be desktop or phone"));
        return;
    }
    with_repo_errors(callback, "wallpapers.list", [&] {
        json out = json::array();
        for (const auto& row : WallpaperRepository().list_ready(device)) {
            out.push_back(public_row(*store, row));
        }
        callback(Response::list(out));
    });
}

void WallpapersController::listAdmin(const HttpRequestPtr& req, Callback&& callback) {
    Storage::ObjectStore* store = store_or_answer(callback);
    if (store == nullptr) {
        return;
    }
    API_REQUIRE_ADMIN(req, callback);
    with_repo_errors(callback, "wallpapers.listAdmin", [&] {
        json out = json::array();
        for (auto row : WallpaperRepository().list_all()) {
            const json pub = public_row(*store, row);
            row["url"] = pub["url"];
            row["poster_url"] = pub["poster_url"];
            out.push_back(row);
        }
        callback(Response::list(out));
    });
}

void WallpapersController::createWallpaper(const HttpRequestPtr& req, Callback&& callback) {
    Storage::ObjectStore* store = store_or_answer(callback);
    if (store == nullptr) {
        return;
    }
    API_REQUIRE_ADMIN(req, callback);
    API_REQUIRE_OWNER(req, callback, admin);
    json body;
    if (!Validation::parse_body(req, body, callback)) {
        return;
    }
    if (!body.is_object()) {
        callback(ErrorResponse::bad_request("invalid_body", "the body must be a JSON object"));
        return;
    }

    Validation::Errors errs;
    for (const char* f : {"kind", "device", "content_type", "bytes"}) {
        Validation::require(errs, body, f);
    }
    Validation::one_of(errs, body, "kind", kKinds);
    Validation::one_of(errs, body, "device", kDevices);
    const std::string kind = body.contains("kind") && body["kind"].is_string() ? body["kind"].get<std::string>() : "";
    const std::string device =
        body.contains("device") && body["device"].is_string() ? body["device"].get<std::string>() : "";
    const bool video = kind == "video";
    if (device == "phone" && video) {
        errs.add("device", "not_allowed", "a phone gets images only");
    }
    if (kind == "video" || kind == "image") {
        Validation::one_of(errs, body, "content_type", video ? kVideoTypes : kImageTypes);
        size_field(errs, body, "bytes", video ? kVideoMax : kImageMax);
    }
    const bool has_poster = body.contains("poster_bytes") && !body["poster_bytes"].is_null();
    if (video && !has_poster) {
        errs.add("poster_bytes", "missing", "a video needs its poster");
    } else if (kind == "image" && has_poster) {
        errs.add("poster_bytes", "not_allowed", "an image has no poster");
    }
    size_field(errs, body, "poster_bytes", kPosterMax);
    Validation::int_range(errs, body, "width", 1, kSideMax);
    Validation::int_range(errs, body, "height", 1, kSideMax);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }

    with_repo_errors(callback, "wallpapers.create", [&] {
        WallpaperRepository repo;
        WallpaperRepository::Input in;
        in.id = repo.new_id();
        in.kind = kind;
        in.device = device;
        in.content_type = body["content_type"].get<std::string>();
        in.object_key = store->key_for("wallpapers", in.id, "file." + extension_of(in.content_type));
        in.bytes = body["bytes"].get<long long>();
        if (video) {
            in.poster_key = store->key_for("wallpapers", in.id, "poster.jpg");
            in.poster_bytes = body["poster_bytes"].get<long long>();
        }
        in.width = opt_side(body, "width");
        in.height = opt_side(body, "height");
        in.created_by = admin;
        repo.create(in);
        json data{{"id", in.id},
                  {"upload", upload_of(store->upload_url(in.object_key, in.content_type, in.bytes), in.content_type)},
                  {"poster_upload", nullptr}};
        if (in.poster_key) {
            data["poster_upload"] =
                upload_of(store->upload_url(*in.poster_key, "image/jpeg", *in.poster_bytes), "image/jpeg");
        }
        callback(Response::created(json{{"data", data}}));
    });
}

void WallpapersController::markReady(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    Storage::ObjectStore* store = store_or_answer(callback);
    if (store == nullptr) {
        return;
    }
    API_REQUIRE_ADMIN(req, callback);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "wallpapers.ready", [&] {
        WallpaperRepository repo;
        const auto row = repo.get(id);
        if (!row) {
            throw Repositories::Now::WallpaperNotFound();
        }
        const auto missing = [&](const json& key, const json& bytes) {
            if (!key.is_string()) {
                return false;
            }
            const auto info = store->head(key.get<std::string>());
            return !info.exists || info.bytes != bytes.get<long long>();
        };
        std::string what;
        try {
            if (missing((*row)["object_key"], (*row)["bytes"])) {
                what = "the file";
            } else if (missing((*row)["poster_key"], (*row)["poster_bytes"])) {
                what = "the poster";
            }
        } catch (const std::exception& e) {
            spdlog::warn("wallpapers.ready: the store did not answer: {}", e.what());
            callback(ErrorResponse::service_unavailable("storage_error", "the storage did not answer; try again"));
            return;
        }
        if (!what.empty()) {
            callback(ErrorResponse::conflict("upload_incomplete",
                                             what + " is not in the bucket at the declared size; upload it again"));
            return;
        }
        callback(Response::ok(json{{"data", public_row(*store, repo.mark_ready(id))}}));
    });
}

void WallpapersController::deleteWallpaper(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    Storage::ObjectStore* store = store_or_answer(callback);
    if (store == nullptr) {
        return;
    }
    API_REQUIRE_ADMIN(req, callback);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "wallpapers.delete", [&] {
        WallpaperRepository repo;
        const auto row = repo.get(id);
        if (!row) {
            throw Repositories::Now::WallpaperNotFound();
        }
        try {
            store->remove((*row)["object_key"].get<std::string>());
            if ((*row)["poster_key"].is_string()) {
                store->remove((*row)["poster_key"].get<std::string>());
            }
        } catch (const std::exception& e) {
            spdlog::warn("wallpapers.delete: the store did not answer: {}", e.what());
            callback(ErrorResponse::service_unavailable("storage_error", "the storage did not answer; try again"));
            return;
        }
        repo.remove(id);
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k204NoContent);
        callback(resp);
    });
}

}  // namespace Api
