/**
 * @file TasksController.cpp
 * @brief The /api/v1/tasks handlers: validation, the owner scope and the
 *        agenda of one local day (docs/superpowers/specs/2026-10-07-tasks-section-design.md).
 */

#include "api/TasksController.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
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
#include "core/Modules.hpp"
#include "jobs/Jobs.hpp"
#include "jobs/TasksParseHandler.hpp"
#include "repositories/tasks/Errors.hpp"
#include "repositories/tasks/NoteRepository.hpp"
#include "repositories/tasks/ParseJobRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "tasks/Agenda.hpp"
#include "tasks/Fields.hpp"
#include "tasks/Llm.hpp"
#include "tasks/ParseAnswer.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
namespace Repo = Repositories::Tasks;
using Api::Fields::date_field;
using Api::Fields::is_calendar_date;
using Api::Fields::text_length;
using Api::Fields::today_utc;

constexpr int kDefaultLimit = 50;
constexpr int kMaxLimit = 200;
constexpr std::size_t kSourceRefMax = 2000;
constexpr std::size_t kExternalIdMax = 100;
constexpr long kParseOpenMax = 3;
const std::vector<std::string> kStatuses = {"open", "done"};
const std::vector<std::string> kNoteStatuses = {"inbox", "archived"};
const std::vector<std::string> kSourceKinds = {"url", "money_transaction"};

std::vector<std::string> listed(const auto& values) {
    return {values.begin(), values.end()};
}

const std::vector<std::string> kAreas = listed(::Tasks::Fields::kAreas);
const std::vector<std::string> kEfforts = listed(::Tasks::Fields::kEfforts);

/// The JSON body, which must be an object: an array or a scalar answers 400, not 500.
bool parse_object(const HttpRequestPtr& req, json& body, TasksController::Callback& callback) {
    if (!Validation::parse_body(req, body, callback)) {
        return false;
    }
    if (!body.is_object()) {
        callback(ErrorResponse::bad_request("invalid_body", "the body must be a JSON object"));
        return false;
    }
    return true;
}

/// Rows belong to an app user; a static-bearer principal has no user id.
bool require_user(const std::string& owner, const TasksController::Callback& callback) {
    if (is_valid_uuid(owner)) {
        return true;
    }
    callback(ErrorResponse::forbidden("no_user_account", "this route needs a user account"));
    return false;
}

/// A field that may be null; when present and not null it must be one of @p allowed.
void nullable_one_of(Validation::Errors& errs,
                     const json& body,
                     const std::string& field,
                     const std::vector<std::string>& allowed) {
    if (body.contains(field) && !body[field].is_null()) {
        Validation::one_of(errs, body, field, allowed);
    }
}

/// The present-or-null value of a nullable string field: outer empty when absent.
std::optional<std::optional<std::string>> nullable(const json& body, const std::string& field) {
    if (!body.contains(field)) {
        return std::nullopt;
    }
    return std::optional<std::optional<std::string>>(std::in_place, Validation::opt_string(body, field));
}

/// The checks of a task body. @p creating requires title and area and allows external_id.
void task_fields(Validation::Errors& errs, const json& body, bool creating) {
    if (creating) {
        Validation::require_string(errs, body, "title");
        Validation::require_string(errs, body, "area");
    }
    text_length(errs, body, "title", 1, ::Tasks::Fields::kTitleMax);
    if (body.contains("area")) {
        Validation::one_of(errs, body, "area", kAreas);
    }
    nullable_one_of(errs, body, "effort", kEfforts);
    if (body.contains("due") && !body["due"].is_null()) {
        date_field(errs, body, "due");
    }
    text_length(errs, body, "next_step", 0, ::Tasks::Fields::kNextStepMax);
    text_length(errs, body, "note", 0, ::Tasks::Fields::kNoteMax);
    if (body.contains("status")) {
        Validation::one_of(errs, body, "status", kStatuses);
    }
    if (creating) {
        text_length(errs, body, "external_id", 1, kExternalIdMax);
    }
}

/// The source pair: both or neither, a link is http(s), a ledger row is a uuid of the caller's.
void source_fields(Validation::Errors& errs, const json& body, const std::string& owner) {
    const bool has_kind = body.contains("source_kind") && !body["source_kind"].is_null();
    const bool has_ref = body.contains("source_ref") && !body["source_ref"].is_null();
    if (!has_kind && !has_ref) {
        return;
    }
    if (has_kind != has_ref) {
        errs.add("source", "pair", "source_kind and source_ref come together");
        return;
    }
    Validation::one_of(errs, body, "source_kind", kSourceKinds);
    text_length(errs, body, "source_ref", 1, kSourceRefMax);
    if (errs.any() || !body["source_ref"].is_string()) {
        return;
    }
    const std::string kind = body["source_kind"].get<std::string>();
    const std::string ref = body["source_ref"].get<std::string>();
    if (kind == "url" && ref.rfind("https://", 0) != 0 && ref.rfind("http://", 0) != 0) {
        errs.add("source_ref", "bad_format", "a link starts with https:// or http://");
    }
    if (kind == "money_transaction") {
        if (!is_valid_uuid(ref)) {
            errs.add("source_ref", "bad_format", "a ledger row id is a uuid");
        } else if (Core::money_enabled() && !Repo::TaskRepository().money_row_is_owners(owner, ref)) {
            errs.add("source_ref", "not_yours", "the ledger row is not yours");
        }
    }
}

Repo::TaskRepository::Input task_input(const json& body) {
    Repo::TaskRepository::Input in;
    in.title = body["title"].get<std::string>();
    in.area = body["area"].get<std::string>();
    in.effort = Validation::opt_string(body, "effort");
    in.due = Validation::opt_string(body, "due");
    in.next_step = Validation::opt_string(body, "next_step").value_or("");
    in.note = Validation::opt_string(body, "note").value_or("");
    in.source_kind = Validation::opt_string(body, "source_kind");
    in.source_ref = Validation::opt_string(body, "source_ref");
    in.external_id = Validation::opt_string(body, "external_id");
    in.status = Validation::opt_string(body, "status").value_or("open");
    return in;
}

}  // namespace

#define TASKS_GUARD(req, callback, owner)    \
    if (!require_enabled(callback))          \
        return;                              \
    API_REQUIRE_OWNER(req, callback, owner); \
    if (!require_user(owner, callback))      \
    return

bool TasksController::require_enabled(const Callback& callback) {
    if (Core::tasks_enabled()) {
        return true;
    }
    callback(ErrorResponse::not_found("tasks"));
    return false;
}

void TasksController::status(const HttpRequestPtr& req, Callback&& callback) {
    TASKS_GUARD(req, callback, owner);
    callback(Response::ok(json{{"data", {{"llm_available", ::Tasks::Llm::parse_settings().has_value()}}}}));
}

// ── items ───────────────────────────────────────────────────────────────────

void TasksController::listItems(const HttpRequestPtr& req, Callback&& callback) {
    TASKS_GUARD(req, callback, owner);
    Repo::TaskRepository::Filter f;
    f.status = req->getParameter("status");
    f.area = req->getParameter("area");
    f.q = req->getParameter("q");
    f.due_to = req->getParameter("due_to");
    f.done_since = req->getParameter("done_since");
    f.limit = clamp_int(req->getParameter("limit"), kDefaultLimit, 1, kMaxLimit);
    f.offset = clamp_int(req->getParameter("offset"), 0, 0, 1000000);
    const auto bad = [](const std::string& v, const std::vector<std::string>& allowed) {
        return !v.empty() && std::find(allowed.begin(), allowed.end(), v) == allowed.end();
    };
    if (bad(f.status, kStatuses) || bad(f.area, kAreas) || (!f.due_to.empty() && !is_calendar_date(f.due_to)) ||
        (!f.done_since.empty() && !is_calendar_date(f.done_since))) {
        callback(ErrorResponse::bad_request("invalid_query",
                                            "status, area, due_to and done_since must be known values and days"));
        return;
    }
    with_repo_errors(callback, "tasks.listItems", [&] {
        const json page = Repo::TaskRepository().list(owner, f);
        callback(Response::ok(
            json{{"data", page["data"]}, {"total", page["total"]}, {"limit", f.limit}, {"offset", f.offset}}));
    });
}

void TasksController::createItem(const HttpRequestPtr& req, Callback&& callback) {
    TASKS_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    task_fields(errs, body, true);
    if (!errs.any()) {
        source_fields(errs, body, owner);
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    const auto in = task_input(body);
    with_repo_errors(callback, "tasks.createItem", [&] {
        callback(Response::created(json{{"data", Repo::TaskRepository().create(owner, in)}}));
    });
}

void TasksController::getItem(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TASKS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "tasks.getItem", [&] {
        const auto row = Repo::TaskRepository().get(owner, id);
        if (!row) {
            callback(ErrorResponse::not_found("task"));
            return;
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

void TasksController::updateItem(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TASKS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    task_fields(errs, body, false);
    if (!errs.any()) {
        source_fields(errs, body, owner);
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    // completed_at is the server's: a body that sends it is not read.
    Repo::TaskRepository::Patch p;
    p.title = Validation::opt_string(body, "title");
    p.area = Validation::opt_string(body, "area");
    p.effort = nullable(body, "effort");
    p.due = nullable(body, "due");
    p.next_step = Validation::opt_string(body, "next_step");
    p.note = Validation::opt_string(body, "note");
    p.status = Validation::opt_string(body, "status");
    p.source_kind = nullable(body, "source_kind");
    p.source_ref = nullable(body, "source_ref");
    with_repo_errors(callback, "tasks.updateItem", [&] {
        callback(Response::ok(json{{"data", Repo::TaskRepository().update(owner, id, p)}}));
    });
}

void TasksController::deleteItem(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TASKS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "tasks.deleteItem", [&] {
        Repo::TaskRepository().remove(owner, id);
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k204NoContent);
        callback(resp);
    });
}

// ── agenda ──────────────────────────────────────────────────────────────────

void TasksController::agenda(const HttpRequestPtr& req, Callback&& callback) {
    TASKS_GUARD(req, callback, owner);
    const std::string date = req->getParameter("date").empty() ? today_utc() : req->getParameter("date");
    const std::string tz = req->getParameter("tz").empty() ? "UTC" : req->getParameter("tz");
    if (!is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_query", "date must be a calendar day"));
        return;
    }
    with_repo_errors(callback, "tasks.agenda", [&] {
        if (!Repo::TaskRepository::is_timezone(tz)) {
            callback(ErrorResponse::bad_request("invalid_query", "tz must be an IANA zone like Asia/Bangkok"));
            return;
        }
        const auto g = ::Tasks::Agenda::group(Repo::TaskRepository().agenda_items(owner, date, tz), date);
        callback(Response::ok(json{{"data",
                                    {{"date", date},
                                     {"tz", tz},
                                     {"late", g.late},
                                     {"soon", g.soon},
                                     {"dated", g.dated},
                                     {"someday", g.someday},
                                     {"done_today", g.done_today},
                                     {"review", {{"no_next_step", g.no_next_step}, {"stale", g.stale}}}}}}));
    });
}

// ── notes ───────────────────────────────────────────────────────────────────

void TasksController::listNotes(const HttpRequestPtr& req, Callback&& callback) {
    TASKS_GUARD(req, callback, owner);
    const std::string status = req->getParameter("status").empty() ? "inbox" : req->getParameter("status");
    if (std::find(kNoteStatuses.begin(), kNoteStatuses.end(), status) == kNoteStatuses.end()) {
        callback(ErrorResponse::bad_request("invalid_query", "status must be inbox or archived"));
        return;
    }
    with_repo_errors(callback, "tasks.listNotes", [&] {
        const json rows = Repo::NoteRepository().list(owner, status);
        callback(Response::ok(json{{"data", rows}, {"count", rows.size()}}));
    });
}

void TasksController::createNote(const HttpRequestPtr& req, Callback&& callback) {
    TASKS_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "text");
    text_length(errs, body, "text", 1, ::Tasks::Fields::kNoteTextMax);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "tasks.createNote", [&] {
        callback(
            Response::created(json{{"data", Repo::NoteRepository().create(owner, body["text"].get<std::string>())}}));
    });
}

void TasksController::updateNote(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TASKS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    text_length(errs, body, "text", 1, ::Tasks::Fields::kNoteTextMax);
    if (body.contains("status")) {
        Validation::one_of(errs, body, "status", kNoteStatuses);
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "tasks.updateNote", [&] {
        callback(Response::ok(
            json{{"data",
                  Repo::NoteRepository().update(
                      owner, id, Validation::opt_string(body, "text"), Validation::opt_string(body, "status"))}}));
    });
}

void TasksController::deleteNote(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TASKS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "tasks.deleteNote", [&] {
        Repo::NoteRepository().remove(owner, id);
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k204NoContent);
        callback(resp);
    });
}

// ── parse ───────────────────────────────────────────────────────────────────

void TasksController::parse(const HttpRequestPtr& req, Callback&& callback) {
    TASKS_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "text");
    text_length(errs, body, "text", 1, ::Tasks::Fields::kPhraseMax);
    date_field(errs, body, "hint_date");
    if (body.contains("note_id") && !body["note_id"].is_null()) {
        Validation::uuid(errs, body, "note_id");
    }
    if (!errs.any() && body["text"].get<std::string>().find('\0') != std::string::npos) {
        errs.add("text", "invalid", "must not contain a NUL character");
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    if (!::Tasks::Llm::parse_settings().has_value()) {
        callback(ErrorResponse::service_unavailable("not_configured", "the tasks parse is not set up on this server"));
        return;
    }
    const std::string text = body["text"].get<std::string>();
    const std::string hint = Validation::opt_string(body, "hint_date").value_or(today_utc());
    const auto note_id = Validation::opt_string(body, "note_id");
    Repo::ParseJobRepository repo;
    std::string id;
    try {
        if (repo.open_count(owner) >= kParseOpenMax) {
            callback(ErrorResponse::make({k429TooManyRequests,
                                          "too_many_parses",
                                          "wait for your running parses to finish",
                                          json{{"retry_after_sec", 30}}}));
            return;
        }
        id = repo.create(owner, text, hint, note_id)["id"].get<std::string>();
    } catch (const Repositories::NotFoundError&) {
        callback(ErrorResponse::not_found("task_note"));
        return;
    } catch (const Repositories::ValidationError& e) {
        callback(ErrorResponse::bad_request(e.code(), e.message()));
        return;
    } catch (const std::exception&) {
        // The server's DETAIL would quote the phrase: no message.
        spdlog::warn("tasks parse create failed");
        callback(ErrorResponse::service_unavailable("storage_unavailable"));
        return;
    }
    try {
        if (!Jobs::is_initialized()) {
            throw std::runtime_error("the job queue is not initialized");
        }
        Jobs::get().submit(
            Jobs::TasksParse::kJobType,
            json{{"job_id", id}, {"owner_id", owner}, {"max_attempts", Jobs::get().default_max_retries()}});
    } catch (const std::exception& e) {
        spdlog::warn("tasks parse enqueue unavailable: {}", e.what());
        try {
            repo.fail(id, "queue_unavailable", "the job queue did not take the job");
        } catch (const std::exception& inner) {
            spdlog::warn("tasks parse {}: could not mark failed: {}", id, inner.what());
        }
        callback(ErrorResponse::service_unavailable("queue_unavailable"));
        return;
    }
    auto resp = Response::ok(json{{"data", {{"id", id}, {"status", "queued"}}}});
    resp->setStatusCode(k202Accepted);
    callback(resp);
}

void TasksController::parseStatus(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TASKS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "tasks.parseStatus", [&] {
        const auto row = Repo::ParseJobRepository().get(owner, id);
        if (!row) {
            callback(ErrorResponse::not_found("task_parse"));
            return;
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

void TasksController::parseAccept(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TASKS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    if (!body.contains("lines") || !body["lines"].is_array() || body["lines"].empty() ||
        body["lines"].size() > static_cast<std::size_t>(::Tasks::Parse::kMaxLines)) {
        callback(ErrorResponse::bad_request("invalid_lines", "lines must hold 1..20 tasks"));
        return;
    }
    std::optional<json> job;
    if (!with_repo_errors(
            callback, "tasks.parseAccept.load", [&] { job = Repo::ParseJobRepository().get(owner, id); })) {
        return;
    }
    if (!job) {
        callback(ErrorResponse::not_found("task_parse"));
        return;
    }
    Validation::Errors errs;
    std::vector<Repo::TaskRepository::Input> inputs;
    for (std::size_t i = 0; i < body["lines"].size(); ++i) {
        json line = body["lines"][i];
        const std::string prefix = "lines[" + std::to_string(i) + "].";
        if (!line.is_object()) {
            errs.add(prefix, "invalid", "a line is an object");
            continue;
        }
        // A draft becomes an open task; its origin is the phrase, not a source.
        for (const char* key : {"status", "source_kind", "source_ref", "external_id"}) {
            line.erase(std::string(key));
        }
        Validation::Errors local;
        task_fields(local, line, true);
        for (const auto& e : local.items()) {
            errs.add(prefix + e.field, e.code, e.message);
        }
        if (!local.any()) {
            inputs.push_back(task_input(line));
        }
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "tasks.parseAccept", [&] {
        const json rows = Repo::ParseJobRepository().accept(owner, id, inputs);
        callback(Response::created(json{{"data", rows}, {"count", rows.size()}}));
    });
}

}  // namespace Api
