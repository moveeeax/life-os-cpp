/**
 * @file GoalsController.cpp
 * @brief The /api/v1/goals handlers: validation per kind, the owner scope,
 *        and each goal answered with its progress at the caller's date
 *        (docs/superpowers/specs/2026-10-08-goals-section-design.md). A number
 *        goal may follow a series of the metrics catalog (`source`); its days
 *        join the check-ins as points
 *        (docs/superpowers/specs/2026-10-20-metric-sources-design.md §3).
 */

#include "api/GoalsController.hpp"

#include <algorithm>
#include <cstdint>
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
#include "domain/Role.hpp"
#include "goals/Fields.hpp"
#include "goals/Progress.hpp"
#include "metrics/Catalog.hpp"
#include "repositories/Zones.hpp"
#include "repositories/goals/Errors.hpp"
#include "repositories/goals/GoalRepository.hpp"
#include "repositories/goals/Sources.hpp"
#include "repositories/metrics/Series.hpp"
#include "security/Auth.hpp"
#include "tasks/Fields.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

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
using Repositories::Goals::Sources;

constexpr double kValueMax = 1e12;
/// UTC+14: the zone whose date is the latest on Earth, the bound of a check-in sent without ?tz=.
constexpr const char* kLatestZone = "Pacific/Kiritimati";

std::vector<std::string> listed(const auto& values) {
    return {values.begin(), values.end()};
}

const std::vector<std::string> kAreas = listed(::Tasks::Fields::kAreas);
const std::vector<std::string> kKinds = listed(::Goals::Fields::kKinds);
const std::vector<std::string> kStatuses = listed(::Goals::Fields::kStatuses);
const std::vector<std::string> kResults = {"pass", "fail"};

/// The date the progress is computed at: ?date=, today in UTC by default; nullopt (answered) when bad.
std::optional<std::string> at_date(const HttpRequestPtr& req, const GoalsController::Callback& callback) {
    const std::string date = request_day(req, &Repositories::Zones::today_in, today_utc());
    if (!is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_query", "date must be a calendar day"));
        return std::nullopt;
    }
    return date;
}

/// The caller's permission bits, the fitness read bit set when the caller passes
/// that check (an admin, or no auth enforced), as /api/v1/metrics counts them.
std::uint32_t permissions_of(const HttpRequestPtr& req) {
    std::uint32_t p = Security::Auth::current_permissions(req);
    if (Security::Auth::require_permission(req, Domain::Permission::kFitnessRead) == nullptr) {
        p |= Domain::Permission::kFitnessRead;
    }
    return p;
}

/// The sources of the request: their days counted in ?tz= (UTC when absent or not a zone).
Sources sources_of(const HttpRequestPtr& req, const std::string& owner) {
    const std::string tz = req->getParameter("tz");
    return Sources(owner, !tz.empty() && Repositories::Zones::is_zone(tz) ? tz : "UTC", permissions_of(req));
}

/// The goal row with its progress block and source_why; @p with_points adds the
/// points of a number goal (`points`, empty for the other kinds).
json with_progress(GoalRepository& repo,
                   Sources& sources,
                   const std::string& owner,
                   json row,
                   const std::string& date,
                   const std::map<std::string, ::Goals::Progress::Counts>& counts,
                   const std::map<std::string, std::vector<::Goals::Progress::Checkin>>* all_points = nullptr,
                   bool with_points = false) {
    const std::string id = row["id"].get<std::string>();
    const auto goal = GoalRepository::progress_goal(row);
    const auto it = counts.find(id);
    const ::Goals::Progress::Counts tasks = it == counts.end() ? ::Goals::Progress::Counts{0, 0} : it->second;
    Sources::Read read;
    if (goal.kind == "number") {
        std::vector<::Goals::Progress::Checkin> checkins;
        if (all_points == nullptr) {
            checkins = repo.checkin_points(owner, id);
        } else if (const auto found = all_points->find(id); found != all_points->end()) {
            checkins = found->second;
        }
        read = sources.points(row, date, checkins);
    }
    row["progress"] = ::Goals::Progress::compute(goal, date, ::Goals::Progress::values_of(read.points), tasks);
    row["source_why"] = read.why;
    if (with_points) {
        row["points"] = Sources::points_json(read.points);
    }
    return row;
}

/// The source of a body: absent, null (no source) or a catalog id. A bad id is a
/// field error; the canonical id is returned for a good one.
std::optional<std::string> source_field(Validation::Errors& errs, const json& body) {
    const auto it = body.find("source");
    if (it == body.end() || it->is_null()) {
        return std::nullopt;
    }
    const auto ref = it->is_string() ? ::Metrics::parse(it->get<std::string>()) : std::nullopt;
    if (!ref) {
        errs.add("source", "invalid", "source is an id of the metrics catalog (GET /api/v1/metrics)");
        return std::nullopt;
    }
    return ::Metrics::canonical(*ref);
}

/// Whether the source names only what is the owner's (a currency of their accounts,
/// their category, their habit ticked by hand); answers 400 not_yours when not.
bool source_is_yours(const std::string& owner,
                     const std::optional<std::string>& source,
                     const GoalsController::Callback& callback) {
    if (!source) {
        return true;
    }
    const auto ref = ::Metrics::parse(*source);
    if (ref && Repositories::Metrics::detail::owns(owner, *ref)) {
        return true;
    }
    Validation::Errors errs;
    errs.add("source", "not_yours", "the source names a currency, category or habit that is not the caller's");
    callback(Validation::response_400(errs));
    return false;
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
    only_for(errs, body, "source", kind, "number");
}

}  // namespace

// ── goals ───────────────────────────────────────────────────────────────────

void GoalsController::listGoals(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::goals_enabled(), "goals", req, callback, owner);
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
        Sources sources = sources_of(req, owner);
        const auto counts = repo.task_counts(owner);
        const auto points = repo.checkin_points_all(owner);
        const json list = repo.list(owner, status);
        json rows = json::array();
        for (const auto& row : list) {
            rows.push_back(with_progress(repo, sources, owner, row, *date, counts, &points));
        }
        callback(Response::ok(json{{"data", rows}, {"count", rows.size()}}));
    });
}

void GoalsController::createGoal(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::goals_enabled(), "goals", req, callback, owner);
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
    const auto source = source_field(errs, body);
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
    // The caller's own today (?tz=), as for a check-in: from midnight to 05:00 in Almaty, UTC is still on yesterday.
    const std::string start = Validation::opt_string(body, "start_date")
                                  .value_or(local_today(req, &Repositories::Zones::today_in, today_utc()));
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
    in.source = source;
    const auto date = at_date(req, callback);
    if (!date) {
        return;
    }
    with_repo_errors(callback, "goals.create", [&] {
        if (!source_is_yours(owner, in.source, callback)) {
            return;
        }
        GoalRepository repo;
        Sources sources = sources_of(req, owner);
        const json row = repo.create(owner, in);
        callback(Response::created(json{{"data", with_progress(repo, sources, owner, row, *date, {})}}));
    });
}

void GoalsController::getGoal(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::goals_enabled(), "goals", req, callback, owner);
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
        Sources sources = sources_of(req, owner);
        json goal = with_progress(repo, sources, owner, *row, *date, repo.task_counts(owner), nullptr, true);
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
    API_MODULE_USER_GUARD(Core::goals_enabled(), "goals", req, callback, owner);
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
        const auto source = source_field(errs, body);
        // A binary goal is done exactly when it has a result (spec §3.2): no closing without
        // one, no reopening with one.
        if (kind == "binary" && body.contains("status") && body["status"].is_string()) {
            const std::string next = body["status"].get<std::string>();
            const bool has_result = (*current)["result"].is_string();
            if (next == "done" && !has_result && !body.contains("result")) {
                errs.add("status", "needs_result", "a pass / fail goal is done when its result is set");
            }
            if (next != "done" && has_result) {
                errs.add("status", "has_result", "a pass / fail goal with a result stays closed");
            }
        }
        if (!errs.any() && body.contains("due") && body["due"].is_string() &&
            body["due"].get<std::string>() <= (*current)["start_date"].get<std::string>()) {
            errs.add("due", "before_start", "due must be after start_date");
        }
        if (errs.any()) {
            callback(Validation::response_400(errs));
            return;
        }
        if (!source_is_yours(owner, source, callback)) {
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
        p.set_source = body.contains("source");
        p.source = source;
        const json row = repo.update(owner, id, p);
        Sources sources = sources_of(req, owner);
        callback(
            Response::ok(json{{"data", with_progress(repo, sources, owner, row, *date, repo.task_counts(owner))}}));
    });
}

void GoalsController::deleteGoal(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::goals_enabled(), "goals", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "goals.delete", [&] {
        GoalRepository().remove(owner, id);
        no_content(callback);
    });
}

// ── check-ins ───────────────────────────────────────────────────────────────

void GoalsController::putCheckin(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::goals_enabled(), "goals", req, callback, owner);
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
    const std::string date =
        Validation::opt_string(body, "date").value_or(local_today(req, &Repositories::Zones::today_in, today_utc()));
    with_repo_errors(callback, "goals.putCheckin", [&] {
        // compute() reads check-ins up to today only, so a later one would be listed and drawn
        // while "now" and the pace ignore it. The caller's today comes from ?tz=; without a
        // zone the bound is today in UTC+14, a date no caller is past yet.
        const std::string tz = req->getParameter("tz");
        std::optional<std::string> latest;
        if (!tz.empty()) {
            latest = Repositories::Zones::today_in(tz);
        }
        if (!latest) {
            latest = Repositories::Zones::today_in(kLatestZone);
        }
        if (latest && date > *latest) {
            callback(ErrorResponse::bad_request("future_date", "a check-in cannot be dated after today"));
            return;
        }
        const json row = GoalRepository().put_checkin(
            owner, id, date, *opt_number(body, "value"), Validation::opt_string(body, "note").value_or(""));
        callback(Response::created(json{{"data", row}}));
    });
}

void GoalsController::deleteCheckin(const HttpRequestPtr& req,
                                    Callback&& callback,
                                    const std::string& id,
                                    const std::string& child) {
    API_MODULE_USER_GUARD(Core::goals_enabled(), "goals", req, callback, owner);
    if (!require_valid_uuid(id, callback) || !require_valid_uuid(child, callback)) {
        return;
    }
    with_repo_errors(callback, "goals.deleteCheckin", [&] {
        GoalRepository().remove_checkin(owner, id, child);
        no_content(callback);
    });
}

// ── sections ────────────────────────────────────────────────────────────────

void GoalsController::addSection(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::goals_enabled(), "goals", req, callback, owner);
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
    API_MODULE_USER_GUARD(Core::goals_enabled(), "goals", req, callback, owner);
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
    API_MODULE_USER_GUARD(Core::goals_enabled(), "goals", req, callback, owner);
    if (!require_valid_uuid(id, callback) || !require_valid_uuid(child, callback)) {
        return;
    }
    with_repo_errors(callback, "goals.deleteSection", [&] {
        GoalRepository().remove_section(owner, id, child);
        no_content(callback);
    });
}

// ── milestones ──────────────────────────────────────────────────────────────

void GoalsController::addMilestone(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::goals_enabled(), "goals", req, callback, owner);
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
    API_MODULE_USER_GUARD(Core::goals_enabled(), "goals", req, callback, owner);
    if (!require_valid_uuid(id, callback) || !require_valid_uuid(child, callback)) {
        return;
    }
    with_repo_errors(callback, "goals.deleteMilestone", [&] {
        GoalRepository().remove_milestone(owner, id, child);
        no_content(callback);
    });
}

}  // namespace Api
