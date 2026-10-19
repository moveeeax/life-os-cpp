/**
 * @file FilesController.cpp
 * @brief The /api/v1/files handlers: the private file store of medical
 *        records and travel documents (FilesController.hpp).
 */

#include "api/FilesController.hpp"

#include <exception>
#include <optional>
#include <string>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/FieldChecks.hpp"
#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "api/Validation.hpp"
#include "repositories/files/FileRepository.hpp"
#include "storage/ObjectStore.hpp"
#include "storage/S3Presign.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Repositories::Files::FileNotFound;
using Repositories::Files::FileRepository;

const std::vector<std::string> kParentKinds = {"medical_record", "travel_document"};
const std::vector<std::string> kContentTypes = {
    "application/pdf", "image/jpeg", "image/png", "image/webp", "image/heic"};

/// The switch of the module a parent kind belongs to.
bool module_on(const std::string& kind) {
    return kind == "medical_record" ? Core::medical_enabled() : Core::travel_enabled();
}

const char* module_of(const std::string& kind) {
    return kind == "medical_record" ? "medical" : "travel";
}

/// The configured store, or nullptr (answered 503).
Storage::ObjectStore* store_or_answer(const FilesController::Callback& callback) {
    Storage::ObjectStore* store = Storage::object_store();
    if (store == nullptr) {
        callback(ErrorResponse::service_unavailable(
            "storage_not_configured", "the file storage is not configured (docs/RUNBOOK.md, private files)"));
    }
    return store;
}

void storage_refused(const FilesController::Callback& callback, const char* op, const std::exception& e) {
    spdlog::warn("{}: the store refused or did not answer: {}", op, e.what());
    callback(ErrorResponse::service_unavailable(
        "storage_unavailable",
        "the file storage refused or did not answer; try again later (docs/RUNBOOK.md, private files)"));
}

std::string extension_of(const std::string& content_type) {
    if (content_type == "application/pdf") {
        return "pdf";
    }
    if (content_type == "image/png") {
        return "png";
    }
    if (content_type == "image/webp") {
        return "webp";
    }
    if (content_type == "image/heic") {
        return "heic";
    }
    return "jpg";
}

/// The name as shown and downloaded: the last path segment, no control characters, trimmed.
std::string clean_name(const std::string& raw) {
    std::string s = raw;
    const auto slash = s.find_last_of("/\\");
    if (slash != std::string::npos) {
        s = s.substr(slash + 1);
    }
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        const auto ch = static_cast<unsigned char>(c);
        if (ch >= 0x20 && ch != 0x7f) {
            out.push_back(c);
        }
    }
    const auto first = out.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "";
    }
    const auto last = out.find_last_not_of(' ');
    return out.substr(first, last - first + 1);
}

/// RFC 6266: an ASCII fallback for old clients and the UTF-8 name for the rest.
std::string disposition_of(const std::string& name) {
    std::string ascii;
    for (const char c : name) {
        const auto ch = static_cast<unsigned char>(c);
        const bool plain = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                           ch == '.' || ch == '-' || ch == '_' || ch == ' ' || ch == '(' || ch == ')';
        ascii.push_back(plain ? c : '_');
    }
    return "inline; filename=\"" + ascii + "\"; filename*=UTF-8''" + Storage::S3::uri_encode(name, false);
}

/// The shape the client sees: no object key.
json public_row(json row) {
    row.erase("object_key");
    return row;
}

/// The owner's row; 404 (answered) when it is not theirs or its module is off.
std::optional<json> owned_row(const std::string& owner,
                              const std::string& id,
                              const FilesController::Callback& callback) {
    std::optional<json> row;
    with_repo_errors(callback, "files.get", [&] {
        row = FileRepository().get(owner, id);
        if (!row) {
            throw FileNotFound();
        }
    });
    if (!row) {
        return std::nullopt;
    }
    const std::string kind = (*row)["parent_kind"].get<std::string>();
    if (!module_on(kind)) {
        callback(ErrorResponse::not_found(module_of(kind)));
        return std::nullopt;
    }
    return row;
}

}  // namespace

void FilesController::listFiles(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    const std::string kind = req->getParameter("parent_kind");
    const std::string parent = req->getParameter("parent_id");
    if (kind != "medical_record" && kind != "travel_document") {
        callback(ErrorResponse::bad_request("invalid_query", "parent_kind must be medical_record or travel_document"));
        return;
    }
    API_REQUIRE_MODULE(module_on(kind), module_of(kind), callback);
    if (!is_valid_uuid(parent)) {
        callback(ErrorResponse::bad_request("invalid_query", "parent_id must be a uuid"));
        return;
    }
    with_repo_errors(callback, "files.list", [&] {
        FileRepository repo;
        if (!repo.parent_exists(owner, kind, parent)) {
            throw Repositories::Files::ParentNotFound();
        }
        json out = json::array();
        for (auto& row : repo.list(owner, kind, parent)) {
            out.push_back(public_row(row));
        }
        callback(Response::list(out));
    });
}

void FilesController::createFile(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    for (const char* f : {"parent_kind", "parent_id", "name", "content_type"}) {
        Validation::require_string(errs, body, f);
    }
    Validation::require(errs, body, "bytes");
    Validation::one_of(errs, body, "parent_kind", kParentKinds);
    Validation::uuid(errs, body, "parent_id");
    Validation::one_of(errs, body, "content_type", kContentTypes);
    Fields::text_length(errs, body, "name", 1, 200);
    if (body.contains("bytes") && !body["bytes"].is_null()) {
        if (!body["bytes"].is_number_integer() || body["bytes"].get<long long>() < 1) {
            errs.add("bytes", "invalid", "bytes must be a positive whole number");
        } else if (body["bytes"].get<long long>() > kMaxBytes) {
            errs.add("bytes", "too_large", "a file is at most " + std::to_string(kMaxBytes) + " bytes");
        }
    }
    std::string name;
    if (body.contains("name") && body["name"].is_string()) {
        name = clean_name(body["name"].get<std::string>());
        if (name.empty() && !errs.any()) {
            errs.add("name", "invalid", "the name has no printable characters");
        }
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    const std::string kind = body["parent_kind"].get<std::string>();
    API_REQUIRE_MODULE(module_on(kind), module_of(kind), callback);
    Storage::ObjectStore* store = store_or_answer(callback);
    if (store == nullptr) {
        return;
    }

    with_repo_errors(callback, "files.create", [&] {
        FileRepository repo;
        FileRepository::Input in;
        in.id = repo.new_id();
        in.parent_kind = kind;
        in.parent_id = body["parent_id"].get<std::string>();
        in.name = name;
        in.content_type = body["content_type"].get<std::string>();
        in.bytes = body["bytes"].get<long long>();
        in.object_key = store->private_key(owner, in.id, "file." + extension_of(in.content_type));
        const json row = repo.create(owner, in);
        const json upload{
            {"url", store->private_upload_url(in.object_key, in.content_type, in.bytes)},
            {"headers", {{"Content-Type", in.content_type}, {"Cache-Control", Storage::kPrivateCacheControl}}}};
        callback(Response::created(json{{"data", {{"file", public_row(row)}, {"upload", upload}}}}));
    });
}

void FilesController::markReady(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_REQUIRE_USER(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    const auto row = owned_row(owner, id, callback);
    if (!row) {
        return;
    }
    if ((*row)["status"] == "ready") {
        callback(Response::ok(json{{"data", public_row(*row)}}));
        return;
    }
    Storage::ObjectStore* store = store_or_answer(callback);
    if (store == nullptr) {
        return;
    }
    const std::string key = (*row)["object_key"].get<std::string>();
    bool there = false;
    bool open_to_all = false;
    try {
        const auto info = store->head(key);
        there = info.exists && info.bytes == (*row)["bytes"].get<long long>();
        // Fail closed: a file is ready only once the bucket is known to refuse an unsigned read.
        open_to_all = there && store->publicly_readable(key);
    } catch (const std::exception& e) {
        storage_refused(callback, "files.ready", e);
        return;
    }
    if (!there) {
        callback(ErrorResponse::conflict("upload_incomplete",
                                         "the file is not in the storage at the declared size; upload it again"));
        return;
    }
    if (open_to_all) {
        spdlog::error("files.ready: {} is readable without a signature; the private prefix is public", key);
        with_repo_errors(callback, "files.ready.public", [&] {
            FileRepository().remove(owner, id);
            try {
                store->remove(key);
            } catch (const std::exception& e) {
                spdlog::warn("files.ready: the store kept {}: {}; the sweep removes it", key, e.what());
            }
            callback(ErrorResponse::service_unavailable(
                "storage_public",
                "the storage lets anyone read private files, so the file was deleted; fix the bucket policy "
                "(docs/RUNBOOK.md, private files)"));
        });
        return;
    }
    with_repo_errors(callback, "files.ready", [&] {
        callback(Response::ok(json{{"data", public_row(FileRepository().mark_ready(owner, id))}}));
    });
}

void FilesController::downloadFile(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_REQUIRE_USER(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    const auto row = owned_row(owner, id, callback);
    if (!row) {
        return;
    }
    if ((*row)["status"] != "ready") {
        callback(ErrorResponse::conflict("not_ready", "the upload of this file has not finished"));
        return;
    }
    Storage::ObjectStore* store = store_or_answer(callback);
    if (store == nullptr) {
        return;
    }
    std::string url;
    try {
        url = store->download_url((*row)["object_key"].get<std::string>(),
                                  kDownloadSeconds,
                                  disposition_of((*row)["name"].get<std::string>()));
    } catch (const std::exception& e) {
        storage_refused(callback, "files.download", e);
        return;
    }
    auto resp = Response::ok(json{{"data", {{"url", url}, {"expires_in", kDownloadSeconds}}}});
    // The link is a bearer credential for two minutes: no cache keeps it.
    resp->addHeader("Cache-Control", "no-store");
    callback(resp);
}

void FilesController::deleteFile(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_REQUIRE_USER(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    if (!owned_row(owner, id, callback)) {
        return;
    }
    // The row goes first, and its key lands in the purges in the same transaction; the object
    // is removed right after, and if the store does not answer, the sweep removes it later.
    std::string key;
    if (!with_repo_errors(callback, "files.delete", [&] { key = FileRepository().remove(owner, id); })) {
        return;
    }
    if (Storage::ObjectStore* store = Storage::object_store()) {
        try {
            store->remove(key);
        } catch (const std::exception& e) {
            spdlog::warn("files.delete: the store kept {}: {}; the sweep removes it", key, e.what());
        }
    }
    no_content(callback);
}

}  // namespace Api
