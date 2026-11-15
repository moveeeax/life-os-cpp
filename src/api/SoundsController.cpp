/**
 * @file SoundsController.cpp
 * @brief The /api/v1/sounds and /api/v1/admin/sounds handlers
 *        (docs/superpowers/specs/2026-10-13-focus-mode-design.md).
 */

#include "api/SoundsController.hpp"

#include <exception>
#include <optional>
#include <string>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/MediaSupport.hpp"
#include "api/RequestUtils.hpp"
#include "api/Validation.hpp"
#include "repositories/now/SoundRepository.hpp"
#include "storage/ObjectStore.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Repositories::Now::SoundRepository;

constexpr long long kSoundMax = 30LL * 1024LL * 1024LL;
constexpr std::size_t kTitleMax = 80;
constexpr int kDurationMax = 24 * 3600;

const std::vector<std::string> kTypes = {"audio/mpeg", "audio/mp4", "audio/x-m4a", "audio/aac", "audio/ogg"};

std::string extension_of(const std::string& content_type) {
    if (content_type == "audio/mpeg") {
        return "mp3";
    }
    if (content_type == "audio/ogg") {
        return "ogg";
    }
    if (content_type == "audio/aac") {
        return "aac";
    }
    return "m4a";
}

json public_row(const Storage::ObjectStore& store, const json& row) {
    return json{{"id", row["id"]},
                {"title", row["title"]},
                {"url", store.public_url(row["object_key"].get<std::string>())},
                {"duration_seconds", row["duration_seconds"]}};
}

}  // namespace

void SoundsController::listSounds(const HttpRequestPtr& req, Callback&& callback) {
    Storage::ObjectStore* store = Media::store_or_answer(callback, "sound");
    if (store == nullptr) {
        return;
    }
    API_REQUIRE_OWNER(req, callback, owner);
    with_repo_errors(callback, "sounds.list", [&] {
        json out = json::array();
        for (const auto& row : SoundRepository().list_ready()) {
            out.push_back(public_row(*store, row));
        }
        callback(Response::list(out));
    });
}

void SoundsController::listAdmin(const HttpRequestPtr& req, Callback&& callback) {
    Storage::ObjectStore* store = Media::store_or_answer(callback, "sound");
    if (store == nullptr) {
        return;
    }
    API_REQUIRE_ADMIN(req, callback);
    with_repo_errors(callback, "sounds.listAdmin", [&] {
        json out = json::array();
        for (auto row : SoundRepository().list_all()) {
            row["url"] = store->public_url(row["object_key"].get<std::string>());
            out.push_back(row);
        }
        callback(Response::list(out));
    });
}

void SoundsController::createSound(const HttpRequestPtr& req, Callback&& callback) {
    Storage::ObjectStore* store = Media::store_or_answer(callback, "sound");
    if (store == nullptr) {
        return;
    }
    API_REQUIRE_ADMIN(req, callback);
    API_REQUIRE_OWNER(req, callback, admin);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    for (const char* f : {"title", "content_type", "bytes"}) {
        Validation::require(errs, body, f);
    }
    Validation::string_length(errs, body, "title", 1, kTitleMax);
    Validation::one_of(errs, body, "content_type", kTypes);
    if (body.contains("bytes") && !body["bytes"].is_null()) {
        if (!body["bytes"].is_number_integer() || body["bytes"].get<long long>() < 1) {
            errs.add("bytes", "invalid", "bytes must be a positive whole number");
        } else if (body["bytes"].get<long long>() > kSoundMax) {
            errs.add("bytes", "too_large", "a sound can be at most " + std::to_string(kSoundMax) + " bytes");
        }
    }
    Validation::int_range(errs, body, "duration_seconds", 1, kDurationMax);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "sounds.create", [&] {
        SoundRepository repo;
        SoundRepository::Input in;
        in.id = repo.new_id();
        in.title = body["title"].get<std::string>();
        in.content_type = body["content_type"].get<std::string>();
        in.object_key = store->key_for("sounds", in.id, "file." + extension_of(in.content_type));
        in.bytes = body["bytes"].get<long long>();
        if (body.contains("duration_seconds") && body["duration_seconds"].is_number_integer()) {
            in.duration_seconds = body["duration_seconds"].get<int>();
        }
        in.created_by = admin;
        repo.create(in);
        const json upload =
            Media::upload_of(store->upload_url(in.object_key, in.content_type, in.bytes), in.content_type);
        callback(Response::created(json{{"data", {{"id", in.id}, {"upload", upload}}}}));
    });
}

void SoundsController::markReady(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    Storage::ObjectStore* store = Media::store_or_answer(callback, "sound");
    if (store == nullptr) {
        return;
    }
    API_REQUIRE_ADMIN(req, callback);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "sounds.ready", [&] {
        SoundRepository repo;
        const auto row = repo.get(id);
        if (!row) {
            throw Repositories::Now::SoundNotFound();
        }
        const long long bytes = (*row)["bytes"].get<long long>();
        bool complete = false;
        try {
            complete = Media::object_matches(*store, (*row)["object_key"].get<std::string>(), bytes);
        } catch (const std::exception& e) {
            Media::storage_error(callback, "sounds.ready", e);
            return;
        }
        if (!complete) {
            callback(ErrorResponse::conflict("upload_incomplete",
                                             "the file is not in the bucket at the declared size; upload it again"));
            return;
        }
        callback(Response::ok(json{{"data", public_row(*store, repo.mark_ready(id))}}));
    });
}

void SoundsController::deleteSound(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    Storage::ObjectStore* store = Media::store_or_answer(callback, "sound");
    if (store == nullptr) {
        return;
    }
    API_REQUIRE_ADMIN(req, callback);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "sounds.delete", [&] {
        SoundRepository repo;
        const auto row = repo.get(id);
        if (!row) {
            throw Repositories::Now::SoundNotFound();
        }
        Media::remove_then_delete(
            repo, *store, id, {(*row)["object_key"].get<std::string>()}, callback, "sounds.delete");
    });
}

}  // namespace Api
