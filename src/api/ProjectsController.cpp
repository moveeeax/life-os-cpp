/**
 * @file ProjectsController.cpp
 * @brief Projects and courses: validation by kind (a project is active, paused
 *        or done; a course is planned, active or done and has a platform and
 *        dates), a code made from the name when none is given.
 */

#include "api/ProjectsController.hpp"

#include <algorithm>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include <drogon/drogon.h>

#include <nlohmann/json.hpp>

#include "api/FieldChecks.hpp"
#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "api/Validation.hpp"
#include "repositories/tasks/Errors.hpp"
#include "repositories/tasks/ProjectRepository.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Api::Fields::date_field;
using Api::Fields::text_length;
using Repositories::Tasks::ProjectRepository;

const std::vector<std::string> kKinds = {"project", "course"};
const std::vector<std::string> kProjectStatuses = {"active", "paused", "done"};
const std::vector<std::string> kCourseStatuses = {"planned", "active", "done"};
const std::vector<std::string> kAllStatuses = {"planned", "active", "paused", "done"};
const std::regex kCode("^[a-z0-9-]{1,24}$");
constexpr std::size_t kCodeMax = 24;

/// An http(s) address or empty; anything else is refused (it becomes a link on the page).
void web_field(Validation::Errors& errs, const json& body, const char* field) {
    text_length(errs, body, field, 0, 500);
    const auto v = Validation::opt_string(body, field);
    if (v && !v->empty() && v->rfind("https://", 0) != 0 && v->rfind("http://", 0) != 0) {
        errs.add(field, "bad_format", "a link starts with https:// or http://");
    }
}

/// The code a name gives: lower-case ASCII letters and digits, other runs as one dash.
std::string code_from(const std::string& name) {
    std::string out;
    for (const unsigned char c : name) {
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (c >= 'A' && c <= 'Z') {
            out.push_back(static_cast<char>(c - 'A' + 'a'));
        } else if (alnum) {
            out.push_back(static_cast<char>(c));
        } else if (!out.empty() && out.back() != '-') {
            out.push_back('-');
        }
        if (out.size() >= kCodeMax) {
            break;
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out.empty() ? "project" : out;
}

/// @p base, or base-2, base-3 … when the owner already has it.
std::string free_code(const std::string& owner, const std::string& base) {
    std::set<std::string> taken;
    for (const auto& p : ProjectRepository().list(owner, "", "")) {
        taken.insert(p["code"].get<std::string>());
    }
    if (!taken.contains(base)) {
        return base;
    }
    for (int i = 2; i < 1000; ++i) {
        const std::string suffix = "-" + std::to_string(i);
        const std::string candidate = base.substr(0, kCodeMax - suffix.size()) + suffix;
        if (!taken.contains(candidate)) {
            return candidate;
        }
    }
    return base;  // the unique index answers 409
}

/// The checks shared by create and edit; @p kind is the project's kind.
void common_fields(Validation::Errors& errs, const json& body, const std::string& kind) {
    text_length(errs, body, "name", 1, 120);
    text_length(errs, body, "description", 0, 500);
    web_field(errs, body, "link");
    if (body.contains("code") && !body["code"].is_null()) {
        if (!body["code"].is_string() || !std::regex_match(body["code"].get<std::string>(), kCode)) {
            errs.add("code", "invalid", "lower-case letters, digits and dashes, up to 24");
        }
    }
    if (body.contains("status")) {
        Validation::one_of(errs, body, "status", kind == "course" ? kCourseStatuses : kProjectStatuses);
    }
    if (kind == "course") {
        text_length(errs, body, "platform", 0, 80);
        web_field(errs, body, "certificate");
        if (body.contains("starts_on") && !body["starts_on"].is_null()) {
            date_field(errs, body, "starts_on");
        }
        if (body.contains("ends_on") && !body["ends_on"].is_null()) {
            date_field(errs, body, "ends_on");
        }
        const auto starts = Validation::opt_string(body, "starts_on");
        const auto ends = Validation::opt_string(body, "ends_on");
        if (!errs.any() && starts && ends && *ends < *starts) {
            errs.add("ends_on", "invalid", "a course ends on or after its start");
        }
    } else {
        for (const char* field : {"platform", "starts_on", "ends_on", "certificate"}) {
            if (body.contains(field) && !body[field].is_null() &&
                !(body[field].is_string() && body[field].get<std::string>().empty())) {
                errs.add(field, "not_allowed", "only a course has it");
            }
        }
    }
}

std::optional<std::optional<std::string>> nullable_date(const json& body, const char* field) {
    if (!body.contains(field)) {
        return std::nullopt;
    }
    return body[field].is_null() ? std::optional<std::string>() : std::optional(body[field].get<std::string>());
}

}  // namespace

void ProjectsController::listProjects(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::tasks_enabled(), "tasks", req, callback, owner);
    const std::string kind = req->getParameter("kind");
    const std::string status = req->getParameter("status");
    const auto known = [](const std::string& v, const std::vector<std::string>& all) {
        return v.empty() || std::find(all.begin(), all.end(), v) != all.end();
    };
    if (!known(kind, kKinds) || !known(status, kAllStatuses)) {
        callback(ErrorResponse::bad_request("invalid_query", "kind and status must be known values"));
        return;
    }
    with_repo_errors(callback, "projects.list", [&] {
        callback(Response::ok(json{{"data", ProjectRepository().list(owner, kind, status)}}));
    });
}

void ProjectsController::createProject(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::tasks_enabled(), "tasks", req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "name");
    if (body.contains("kind")) {
        Validation::one_of(errs, body, "kind", kKinds);
    }
    const std::string kind = body.value("kind", std::string("project")) == "course" ? "course" : "project";
    common_fields(errs, body, kind);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "projects.create", [&] {
        ProjectRepository::Input in;
        in.kind = kind;
        in.name = body["name"].get<std::string>();
        const auto code = Validation::opt_string(body, "code");
        in.code = code && !code->empty() ? *code : free_code(owner, code_from(in.name));
        in.description = Validation::opt_string(body, "description").value_or("");
        in.link = Validation::opt_string(body, "link").value_or("");
        in.status = Validation::opt_string(body, "status").value_or(kind == "course" ? "planned" : "active");
        in.platform = Validation::opt_string(body, "platform").value_or("");
        in.starts_on = Validation::opt_string(body, "starts_on");
        in.ends_on = Validation::opt_string(body, "ends_on");
        in.certificate = Validation::opt_string(body, "certificate").value_or("");
        callback(Response::created(json{{"data", ProjectRepository().create(owner, in)}}));
    });
}

void ProjectsController::getProject(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::tasks_enabled(), "tasks", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "projects.get", [&] {
        const auto row = ProjectRepository().get(owner, id);
        if (!row) {
            throw Repositories::Tasks::NotFound("project");
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

void ProjectsController::updateProject(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::tasks_enabled(), "tasks", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    with_repo_errors(callback, "projects.update", [&] {
        ProjectRepository repo;
        const auto row = repo.get(owner, id);
        if (!row) {
            throw Repositories::Tasks::NotFound("project");
        }
        // The kind is fixed at creation: a course stays a course.
        const std::string kind = (*row)["kind"].get<std::string>();
        Validation::Errors errs;
        if (body.contains("kind") && body["kind"] != kind) {
            errs.add("kind", "not_allowed", "the kind of a project does not change");
        }
        common_fields(errs, body, kind);
        if (errs.any()) {
            callback(Validation::response_400(errs));
            return;
        }
        ProjectRepository::Patch p;
        p.name = Validation::opt_string(body, "name");
        p.code = Validation::opt_string(body, "code");
        p.description = Validation::opt_string(body, "description");
        p.link = Validation::opt_string(body, "link");
        p.status = Validation::opt_string(body, "status");
        if (kind == "course") {
            p.platform = Validation::opt_string(body, "platform");
            p.starts_on = nullable_date(body, "starts_on");
            p.ends_on = nullable_date(body, "ends_on");
            p.certificate = Validation::opt_string(body, "certificate");
        }
        callback(Response::ok(json{{"data", repo.update(owner, id, p)}}));
    });
}

void ProjectsController::deleteProject(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::tasks_enabled(), "tasks", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "projects.delete", [&] {
        ProjectRepository().remove(owner, id);
        no_content(callback);
    });
}

}  // namespace Api
