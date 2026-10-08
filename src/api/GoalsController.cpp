/**
 * @file GoalsController.cpp
 * @brief The /api/v1/goals handlers: validation per kind, the owner scope,
 *        and each goal answered with its progress at the caller's date
 *        (docs/superpowers/specs/2026-10-08-goals-section-design.md).
 */

#include "api/GoalsController.hpp"

#include <algorithm>
#include <map>
#include <optional>
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
#include "goals/Fields.hpp"
#include "goals/Progress.hpp"
#include "repositories/goals/Errors.hpp"
#include "repositories/goals/GoalRepository.hpp"
#include "tasks/Fields.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Api::Fields::date_field;
using Api::Fields::is_calendar_date;
using Api::Fields::number_range;
using Api::Fields::opt_int;
using Api::Fields::opt_number;
using Api::Fields::text_length;
using Api::Fields::today_utc;
using Repositories::Goals::GoalRepository;

constexpr double kValueMax = 1e12;

std::vector<std::string> listed(const auto& values) {
    return {values.begin(), values.end()};
}

const std::vector<std::string> kAreas = listed(::Tasks::Fields::kAreas);
const std::vector<std::string> kKinds = listed(::Goals::Fields::kKinds);
const std::vector<std::string> kStatuses = listed(::Goals::Fields::kStatuses);
const std::vector<std::string> kResults = {"pass", "fail"};

/// The JSON body, which must be an object: an array or a scalar answers 400, not 500.
bool parse_object(const HttpRequestPtr& req, json& body, GoalsController::Callback& callback) {
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
bool require_user(const std::string& owner, const GoalsController::Callback& callback) {
    if (is_valid_uuid(owner)) {
        return true;
    }
    callback(ErrorResponse::forbidden("no_user_account", "this route needs a user account"));
    return false;
}

/// The date the progress is computed at: ?date=, today in UTC by default; nullopt (answered) when bad.
std::optional<std::string> at_date(const HttpRequestPtr& req, const GoalsController::Callback& callback) {
    const std::string date = req->getParameter("date").empty() ? today_utc() : req->getParameter("date");
    if (!is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_query", "date must be a calendar day"));
        return std::nullopt;
    }
    return date;
}

/// The goal row with its progress block.
json with_progress(GoalRepository& repo,
                   const std::string& owner,
                   json row,
                   const std::string& date,
                   const std::map<std::string, ::Goals::Progress::Counts>& counts) {
    const std::string id = row["id"].get<std::string>();
    const auto goal = GoalRepository::progress_goal(row);
    const auto it = counts.find(id);
    const ::Goals::Progress::Counts tasks = it == counts.end() ? ::Goals::Progress::Counts{0, 0} : it->second;
    const std::vector<::Goals::Progress::Checkin> points =
        goal.kind == "number" ? repo.checkin_points(owner, id) : std::vector<::Goals::Progress::Checkin>{};
    row["progress"] = ::Goals::Progress::compute(goal, date, points, tasks);
    return row;
}

/// A field that only one kind takes: present on another kind is an error.
void only_for(
    Validation::Errors& errs, const json& body, const char* field, const std::string& kind, const char* owner_kind) {
    if (kind != owner_kind && body.contains(field) && !body[field].is_null()) {
        errs.add(field, "not_allowed", std::string("only a ") + owner_kind + " goal takes " + field);
    }
}

/// The checks shared by create and update. @p kind is the goal's kind (given on create, stored on update).
void goal_fields(Validation::Errors& errs, const json& body, const std::string& kind) {
    text_length(errs, body, "title", 1, ::Goals::Fields::kTitleMax);
    if (body.contains("area")) {
        Validation::one_of(errs, body, "area", kAreas);
    }
    text_length(errs, body, "why", 0, ::Goals::Fields::kWhyMax);
    date_field(errs, body, "due");
    if (body.contains("status")) {
        Validation::one_of(errs, body, "status", kStatuses);
    }
    text_length(errs, body, "unit", 0, ::Goals::Fields::kUnitMax);
    number_range(errs, body, "start_value", -kValueMax, kValueMax);
    number_range(errs, body, "target_value", -kValueMax, kValueMax);
    Validation::int_range(errs, body, "target_count", 1, ::Goals::Fields::kTargetCountMax);
    if (body.contains("result")) {
        if (body["result"].is_null()) {
            errs.add("result", "not_allowed", "a result is not taken back");
        } else {
            Validation::one_of(errs, body, "result", kResults);
        }
    }
    if (body.contains("due") && body["due"].is_null()) {
        errs.add("due", "missing", "a goal keeps a due date");
    }
    only_for(errs, body, "unit", kind, "number");
    only_for(errs, body, "start_value", kind, "number");
    only_for(errs, body, "target_value", kind, "number");
    only_for(errs, body, "target_count", kind, "count");
    only_for(errs, body, "result", kind, "binary");
}

}  // namespace

#define GOALS_GUARD(req, callback, owner)    \
    if (!require_enabled(callback))          \
        return;                              \
    API_REQUIRE_OWNER(req, callback, owner); \
    if (!require_user(owner, callback))      \
    return

bool GoalsController::require_enabled(const Callback& callback) {
    if (Core::goals_enabled()) {
        return true;
    }
    callback(ErrorResponse::not_found("goals"));
    return false;
}

// ── goals ───────────────────────────────────────────────────────────────────

void GoalsController::listGoals(const HttpRequestPtr& req, Callback&& callback) {
    GOALS_GUARD(req, callback, owner);
    const auto date = at_date(req, callback);
    if (!date) {
        return;
    }
    const std::string status = req->getParameter("status");
    if (!status.empty() && !::Goals::Fields::is_status(status)) {
        callback(ErrorResponse::bad_request("invalid_query", "status must be active, done or dropped"));
        return;
    }
    with_repo_errors(callback, "goals.list", [&] {
        GoalRepository repo;
        const auto counts = repo.task_counts(owner);
        json rows = json::array();
        for (const auto& row : repo.list(owner, status)) {
            rows.push_back(with_progress(repo, owner, row, *date, counts));
        }
        callback(Response::ok(json{{"data", rows}, {"count", rows.size()}}));
    });
}

void GoalsController::createGoal(const HttpRequestPtr& req, Callback&& callback) {
    GOALS_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "title");
    Validation::require_string(errs, body, "area");
    Validation::require_string(errs, body, "kind");
    Validation::require_string(errs, body, "due");
    if (body.contains("kind")) {
        Validation::one_of(errs, body, "kind", kKinds);
    }
    date_field(errs, body, "start_date");
    const std::string kind = Validation::opt_string(body, "kind").value_or("");
    goal_fields(errs, body, kind);
    if (body.contains("status") || body.contains("result")) {
        errs.add("status", "not_allowed", "a new goal is active and has no result");
    }
    if (!errs.any() && kind == "number" && (!opt_number(body, "start_value") || !opt_number(body, "target_value"))) {
        errs.add("target_value", "missing", "a number goal needs start_value and target_value");
    }
    if (!errs.any() && kind == "number" && *opt_number(body, "start_value") == *opt_number(body, "target_value")) {
        errs.add("target_value", "invalid", "the target must differ from the start");
    }
    if (!errs.any() && kind == "count" && !opt_int(body, "target_count")) {
        errs.add("target_count", "missing", "a count goal needs target_count");
    }
    const std::string start = Validation::opt_string(body, "start_date").value_or(today_utc());
    if (!errs.any() && body["due"].get<std::string>() <= start) {
        errs.add("due", "before_start", "due must be after start_date");
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    GoalRepository::Input in;
    in.title = body["title"].get<std::string>();
    in.area = body["area"].get<std::string>();
    in.kind = kind;
    in.why = Validation::opt_string(body, "why").value_or("");
    in.start_date = start;
    in.due = body["due"].get<std::string>();
    in.unit = Validation::opt_string(body, "unit").value_or("");
    in.start_value = opt_number(body, "start_value");
    in.target_value = opt_number(body, "target_value");
    in.target_count = opt_int(body, "target_count");
    const auto date = at_date(req, callback);
    if (!date) {
        return;
    }
    with_repo_errors(callback, "goals.create", [&] {
        GoalRepository repo;
        const json row = repo.create(owner, in);
        callback(Response::created(json{{"data", with_progress(repo, owner, row, *date, {})}}));
    });
}

void GoalsController::getGoal(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    GOALS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    const auto date = at_date(req, callback);
    if (!date) {
        return;
    }
    with_repo_errors(callback, "goals.get", [&] {
        GoalRepository repo;
        const auto row = repo.get(owner, id);
        if (!row) {
            callback(ErrorResponse::not_found("goal"));
            return;
        }
        json goal = with_progress(repo, owner, *row, *date, repo.task_counts(owner));
        goal["checkins"] = repo.checkins(owner, id);
        goal["milestones"] = repo.milestones(owner, id);
        // Sections with their tasks; the tasks without a section apart.
        json sections = repo.sections(owner, id);
        json loose = json::array();
        std::map<std::string, std::size_t> at;
        for (std::size_t i = 0; i < sections.size(); ++i) {
            sections[i]["tasks"] = json::array();
            at[sections[i]["id"].get<std::string>()] = i;
        }
        for (const auto& t : repo.tasks_of(owner, id)) {
            const auto it =
                t["goal_section_id"].is_string() ? at.find(t["goal_section_id"].get<std::string>()) : at.end();
            if (it == at.end()) {
                loose.push_back(t);
            } else {
                sections[it->second]["tasks"].push_back(t);
            }
        }
        goal["sections"] = sections;
        goal["tasks"] = loose;
        callback(Response::ok(json{{"data", goal}}));
    });
}

void GoalsController::updateGoal(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    GOALS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto date = at_date(req, callback);
    if (!date) {
        return;
    }
    with_repo_errors(callback, "goals.update", [&] {
        GoalRepository repo;
        const auto current = repo.get(owner, id);
        if (!current) {
            callback(ErrorResponse::not_found("goal"));
            return;
        }
        const std::string kind = (*current)["kind"].get<std::string>();
        Validation::Errors errs;
        if (body.contains("kind") && body["kind"] != kind) {
            errs.add("kind", "immutable", "the kind of a goal does not change");
        }
        if (body.contains("start_date")) {
            errs.add("start_date", "immutable", "the start of a goal does not change");
        }
        goal_fields(errs, body, kind);
        if (!errs.any() && body.contains("due") && body["due"].is_string() &&
            body["due"].get<std::string>() <= (*current)["start_date"].get<std::string>()) {
            errs.add("due", "before_start", "due must be after start_date");
        }
        if (errs.any()) {
            callback(Validation::response_400(errs));
            return;
        }
        GoalRepository::Patch p;
        p.title = Validation::opt_string(body, "title");
        p.area = Validation::opt_string(body, "area");
        p.why = Validation::opt_string(body, "why");
        p.due = Validation::opt_string(body, "due");
        p.status = Validation::opt_string(body, "status");
        p.unit = Validation::opt_string(body, "unit");
        p.start_value = opt_number(body, "start_value");
        p.target_value = opt_number(body, "target_value");
        p.target_count = opt_int(body, "target_count");
        p.result = Validation::opt_string(body, "result");
        const json row = repo.update(owner, id, p);
        callback(Response::ok(json{{"data", with_progress(repo, owner, row, *date, repo.task_counts(owner))}}));
    });
}

void GoalsController::deleteGoal(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    GOALS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "goals.delete", [&] {
        GoalRepository().remove(owner, id);
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k204NoContent);
        callback(resp);
    });
}

// ── check-ins ───────────────────────────────────────────────────────────────

void GoalsController::putCheckin(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    GOALS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    if (!opt_number(body, "value")) {
        errs.add("value", "missing", "value is a number");
    }
    number_range(errs, body, "value", -kValueMax, kValueMax);
    date_field(errs, body, "date");
    text_length(errs, body, "note", 0, ::Goals::Fields::kNoteMax);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    const std::string date = Validation::opt_string(body, "date").value_or(today_utc());
    with_repo_errors(callback, "goals.putCheckin", [&] {
        const json row = GoalRepository().put_checkin(
            owner, id, date, *opt_number(body, "value"), Validation::opt_string(body, "note").value_or(""));
        callback(Response::created(json{{"data", row}}));
    });
}

void GoalsController::deleteCheckin(const HttpRequestPtr& req,
                                    Callback&& callback,
                                    const std::string& id,
                                    const std::string& child) {
    GOALS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback) || !require_valid_uuid(child, callback)) {
        return;
    }
    with_repo_errors(callback, "goals.deleteCheckin", [&] {
        GoalRepository().remove_checkin(owner, id, child);
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k204NoContent);
        callback(resp);
    });
}

// ── sections ────────────────────────────────────────────────────────────────

void GoalsController::addSection(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    GOALS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "name");
    text_length(errs, body, "name", 1, ::Goals::Fields::kNameMax);
    Validation::int_range(errs, body, "position", 0, 10000);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "goals.addSection", [&] {
        callback(Response::created(json{
            {"data",
             GoalRepository().add_section(owner, id, body["name"].get<std::string>(), opt_int(body, "position"))}}));
    });
}

void GoalsController::updateSection(const HttpRequestPtr& req,
                                    Callback&& callback,
                                    const std::string& id,
                                    const std::string& child) {
    GOALS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback) || !require_valid_uuid(child, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    text_length(errs, body, "name", 1, ::Goals::Fields::kNameMax);
    Validation::int_range(errs, body, "position", 0, 10000);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "goals.updateSection", [&] {
        callback(Response::ok(
            json{{"data",
                  GoalRepository().update_section(
                      owner, id, child, Validation::opt_string(body, "name"), opt_int(body, "position"))}}));
    });
}

void GoalsController::deleteSection(const HttpRequestPtr& req,
                                    Callback&& callback,
                                    const std::string& id,
                                    const std::string& child) {
    GOALS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback) || !require_valid_uuid(child, callback)) {
        return;
    }
    with_repo_errors(callback, "goals.deleteSection", [&] {
        GoalRepository().remove_section(owner, id, child);
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k204NoContent);
        callback(resp);
    });
}

// ── milestones ──────────────────────────────────────────────────────────────

void GoalsController::addMilestone(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    GOALS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "date");
    Validation::require_string(errs, body, "label");
    date_field(errs, body, "date");
    text_length(errs, body, "label", 1, ::Goals::Fields::kNameMax);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "goals.addMilestone", [&] {
        callback(Response::created(
            json{{"data",
                  GoalRepository().add_milestone(
                      owner, id, body["date"].get<std::string>(), body["label"].get<std::string>())}}));
    });
}

void GoalsController::deleteMilestone(const HttpRequestPtr& req,
                                      Callback&& callback,
                                      const std::string& id,
                                      const std::string& child) {
    GOALS_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback) || !require_valid_uuid(child, callback)) {
        return;
    }
    with_repo_errors(callback, "goals.deleteMilestone", [&] {
        GoalRepository().remove_milestone(owner, id, child);
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k204NoContent);
        callback(resp);
    });
}

}  // namespace Api
