/**
 * @file NotesController.cpp
 * @brief Notes: a slug made from the title when none is given (kept unique per
 *        owner), tags as a list (no commas: tarassov.me stores them joined), a
 *        project, course or task link of the caller only; private for now.
 */

#include "api/NotesController.hpp"

#include <algorithm>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <drogon/drogon.h>

#include <nlohmann/json.hpp>

#include "api/FieldChecks.hpp"
#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "api/Validation.hpp"
#include "core/Modules.hpp"
#include "notes/Slug.hpp"
#include "repositories/notes/NoteRepository.hpp"
#include "repositories/tasks/ProjectRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Utf8.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Api::Fields::text_length;
using Repositories::Notes::NoteRepository;

const std::regex kSlug("^[a-z0-9]+(-[a-z0-9]+)*$");
constexpr std::size_t kTagsMax = 20;
constexpr std::size_t kTagMax = 40;
constexpr std::size_t kBodyMax = 200000;
constexpr int kDefaultLimit = 50;
constexpr int kMaxLimit = 200;

bool parse_object(const HttpRequestPtr& req, json& body, NotesController::Callback& callback) {
    if (!Validation::parse_body(req, body, callback)) {
        return false;
    }
    if (!body.is_object()) {
        callback(ErrorResponse::bad_request("invalid_body", "the body must be a JSON object"));
        return false;
    }
    return true;
}

bool require_user(const std::string& owner, const NotesController::Callback& callback) {
    if (is_valid_uuid(owner)) {
        return true;
    }
    callback(ErrorResponse::forbidden("no_user_account", "this route needs a user account"));
    return false;
}

#define NOTES_GUARD(req, callback, owner)            \
    if (!Core::notes_enabled()) {                    \
        callback(ErrorResponse::not_found("notes")); \
        return;                                      \
    }                                                \
    API_REQUIRE_OWNER(req, callback, owner);         \
    if (!require_user(owner, callback))              \
    return

int clamp(const std::string& text, int fallback, int lo, int hi) {
    try {
        return text.empty() ? fallback : std::clamp(std::stoi(text), lo, hi);
    } catch (const std::exception&) {
        return fallback;
    }
}

/// The tags of a body, trimmed; nullopt (with an error) when they break a rule.
std::optional<std::vector<std::string>> tags_field(Validation::Errors& errs, const json& body) {
    if (!body.contains("tags")) {
        return std::nullopt;
    }
    if (!body["tags"].is_array() || body["tags"].size() > kTagsMax) {
        errs.add("tags", "invalid", "a list of up to 20 tags");
        return std::nullopt;
    }
    std::vector<std::string> out;
    for (const auto& t : body["tags"]) {
        if (!t.is_string()) {
            errs.add("tags", "invalid", "each tag is text");
            return std::nullopt;
        }
        std::string tag = t.get<std::string>();
        const auto b = tag.find_first_not_of(" \t");
        const auto e = tag.find_last_not_of(" \t");
        tag = b == std::string::npos ? "" : tag.substr(b, e - b + 1);
        if (tag.empty() || Utils::Utf8::length(tag) > kTagMax || tag.find(',') != std::string::npos) {
            errs.add("tags", "invalid", "each tag has 1 to 40 characters and no comma");
            return std::nullopt;
        }
        if (std::find(out.begin(), out.end(), tag) == out.end()) {
            out.push_back(tag);
        }
    }
    return out;
}

/// Shared checks of create and edit; the links must be the caller's.
void note_fields(Validation::Errors& errs, const json& body, const std::string& owner, bool create) {
    if (create) {
        Validation::require_string(errs, body, "title");
    }
    text_length(errs, body, "title", 1, 200);
    text_length(errs, body, "summary", 0, 500);
    text_length(errs, body, "body", 0, kBodyMax);
    text_length(errs, body, "topic", 0, 60);
    if (body.contains("slug") && !body["slug"].is_null()) {
        if (!body["slug"].is_string() || body["slug"].get<std::string>().size() > Notes::kSlugMax ||
            !std::regex_match(body["slug"].get<std::string>(), kSlug)) {
            errs.add("slug", "invalid", "lower-case letters and digits joined by single dashes, up to 120");
        }
    }
    if (body.contains("status") && body["status"] != "private") {
        errs.add("status", "not_yet", "notes are private for now; publishing to the blog comes later");
    }
    for (const char* link : {"project_id", "task_id"}) {
        if (body.contains(link) && !body[link].is_null()) {
            Validation::uuid(errs, body, link);
        }
    }
    if (errs.any()) {
        return;
    }
    if (body.contains("project_id") && !body["project_id"].is_null() &&
        !Repositories::Tasks::ProjectRepository().owns(owner, body["project_id"].get<std::string>())) {
        errs.add("project_id", "not_yours", "the project is not yours");
    }
    if (body.contains("task_id") && !body["task_id"].is_null() &&
        !Repositories::Tasks::TaskRepository().get(owner, body["task_id"].get<std::string>())) {
        errs.add("task_id", "not_yours", "the task is not yours");
    }
}

std::optional<std::optional<std::string>> nullable(const json& body, const char* field) {
    if (!body.contains(field)) {
        return std::nullopt;
    }
    return body[field].is_null() ? std::optional<std::string>() : std::optional(body[field].get<std::string>());
}

void no_content(const NotesController::Callback& callback) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    callback(resp);
}

}  // namespace

void NotesController::listNotes(const HttpRequestPtr& req, Callback&& callback) {
    NOTES_GUARD(req, callback, owner);
    NoteRepository::Filter f;
    f.q = req->getParameter("q");
    f.topic = req->getParameter("topic");
    f.tag = req->getParameter("tag");
    f.project = req->getParameter("project");
    f.limit = clamp(req->getParameter("limit"), kDefaultLimit, 1, kMaxLimit);
    f.offset = clamp(req->getParameter("offset"), 0, 0, 1000000);
    if (!f.project.empty() && !is_valid_uuid(f.project)) {
        callback(ErrorResponse::bad_request("invalid_query", "project must be an id"));
        return;
    }
    with_repo_errors(callback, "notes.list", [&] {
        NoteRepository repo;
        json out = repo.list(owner, f);
        const json facets = repo.topics(owner);
        out["topics"] = facets["topics"];
        out["tags"] = facets["tags"];
        callback(Response::ok(out));
    });
}

void NotesController::createNote(const HttpRequestPtr& req, Callback&& callback) {
    NOTES_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    note_fields(errs, body, owner, true);
    const auto tags = tags_field(errs, body);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "notes.create", [&] {
        NoteRepository repo;
        NoteRepository::Input in;
        in.title = body["title"].get<std::string>();
        const auto slug = Validation::opt_string(body, "slug");
        in.slug = slug ? *slug : Notes::free_slug(Notes::slug_of(in.title), repo.slugs(owner));
        in.summary = Validation::opt_string(body, "summary").value_or("");
        in.body = Validation::opt_string(body, "body").value_or("");
        in.topic = Validation::opt_string(body, "topic").value_or("");
        in.tags = tags.value_or(std::vector<std::string>{});
        in.project_id = Validation::opt_string(body, "project_id");
        in.task_id = Validation::opt_string(body, "task_id");
        callback(Response::created(json{{"data", repo.create(owner, in)}}));
    });
}

void NotesController::getNote(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    NOTES_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "notes.get", [&] {
        const auto row = NoteRepository().get(owner, id);
        if (!row) {
            throw Repositories::Notes::NotFound();
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

void NotesController::updateNote(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    NOTES_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    note_fields(errs, body, owner, false);
    const auto tags = tags_field(errs, body);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "notes.update", [&] {
        NoteRepository::Patch p;
        p.slug = Validation::opt_string(body, "slug");
        p.title = Validation::opt_string(body, "title");
        p.summary = Validation::opt_string(body, "summary");
        p.body = Validation::opt_string(body, "body");
        p.topic = Validation::opt_string(body, "topic");
        p.tags = tags;
        p.project_id = nullable(body, "project_id");
        p.task_id = nullable(body, "task_id");
        callback(Response::ok(json{{"data", NoteRepository().update(owner, id, p)}}));
    });
}

void NotesController::deleteNote(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    NOTES_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "notes.delete", [&] {
        NoteRepository().remove(owner, id);
        no_content(callback);
    });
}

}  // namespace Api
