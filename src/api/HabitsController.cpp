/**
 * @file HabitsController.cpp
 * @brief The /api/v1/habits handlers: validation, the owner scope, the manual
 *        ticks, the days evaluated with the auto values of Mi Fitness and
 *        Training (docs/superpowers/specs/2026-10-14-habits-design.md).
 */

#include "api/HabitsController.hpp"

#include <chrono>
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
#include "domain/Role.hpp"
#include "habits/Habits.hpp"
#include "repositories/habits/Days.hpp"
#include "repositories/habits/HabitRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "security/Auth.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Api::Fields::is_calendar_date;
using Api::Fields::opt_int;
using Api::Fields::text_length;
using Repositories::Habits::HabitRepository;

/// The longest range GET /habits/days answers, in days.
constexpr int kRangeMax = 62;
/// How far back a tick may go, in days.
constexpr int kTickBackMax = 366;
/// The largest goal: a step count with room to spare.
constexpr double kGoalMax = 1000000;
constexpr double kSleepGoalMax = 24;

const std::vector<std::string> kSchedules = {"daily", "weekly"};
const std::vector<std::string> kSources = {"manual", "sleep", "steps", "active_kcal", "workout"};

/// ?tz=, UTC by default; nullopt (answered) when not an IANA zone.
std::optional<std::string> zone_of(const HttpRequestPtr& req, const HabitsController::Callback& callback) {
    const std::string tz = req->getParameter("tz").empty() ? "UTC" : req->getParameter("tz");
    if (!Repositories::Tasks::TaskRepository::is_timezone(tz)) {
        callback(ErrorResponse::bad_request("invalid_query", "tz must be an IANA zone like Asia/Almaty"));
        return std::nullopt;
    }
    return tz;
}

/// A habit body, checked; nullopt (answered) when bad.
std::optional<HabitRepository::Input> habit_input(const json& body, const HabitsController::Callback& callback) {
    Validation::Errors errs;
    Validation::require_string(errs, body, "name");
    Validation::require_string(errs, body, "schedule");
    text_length(errs, body, "name", 1, 80);
    text_length(errs, body, "unit", 0, 16);
    Validation::one_of(errs, body, "schedule", kSchedules);
    Validation::one_of(errs, body, "source", kSources);
    const std::string schedule = Validation::opt_string(body, "schedule").value_or("");
    const std::string source = Validation::opt_string(body, "source").value_or("manual");
    const auto per_week = opt_int(body, "per_week");
    if (schedule == "weekly" && (!per_week || *per_week < 1 || *per_week > 7)) {
        errs.add("per_week", "invalid", "a weekly habit needs per_week from 1 to 7");
    }
    if (schedule == "daily" && body.contains("per_week") && !body["per_week"].is_null()) {
        errs.add("per_week", "not_allowed", "only a weekly habit takes per_week");
    }
    std::optional<double> goal;
    if (body.contains("goal") && !body["goal"].is_null()) {
        const double max = source == "sleep" ? kSleepGoalMax : kGoalMax;
        if (!body["goal"].is_number() || !(body["goal"].get<double>() > 0) || body["goal"].get<double>() > max) {
            errs.add("goal", "invalid", "a goal is a number above 0, at most " + json(max).dump());
        } else {
            goal = body["goal"].get<double>();
        }
    }
    if ((source == "sleep" || source == "steps" || source == "active_kcal") && !goal &&
        !(body.contains("goal") && !body["goal"].is_null())) {
        errs.add("goal", "missing", "a source from Mi needs a goal");
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return std::nullopt;
    }
    // A workout is any finished session that day: its goal would mean nothing.
    if (source == "workout") {
        goal.reset();
    }
    return HabitRepository::Input{body["name"].get<std::string>(),
                                  schedule,
                                  schedule == "weekly" ? per_week : std::nullopt,
                                  goal,
                                  Validation::opt_string(body, "unit").value_or(""),
                                  source};
}

/// The day of a tick, checked against today in the caller's zone; false (answered) when bad.
bool tick_day(const std::string& date, const std::string& tz, const HabitsController::Callback& callback) {
    if (!is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_date", "the date must be a calendar day as YYYY-MM-DD"));
        return false;
    }
    const auto today = ::Habits::day_of(HabitRepository().today(tz));
    const auto day = ::Habits::day_of(date);
    if (day > today) {
        callback(ErrorResponse::bad_request("future_date", "a day after today cannot be ticked"));
        return false;
    }
    if ((today - day).count() > kTickBackMax) {
        callback(ErrorResponse::bad_request("date_out_of_range", "a tick reaches back at most 366 days"));
        return false;
    }
    return true;
}

}  // namespace

void HabitsController::list(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::habits_enabled(), "habits", req, callback, owner);
    const std::string archived = req->getParameter("archived");
    if (!archived.empty() && archived != "true" && archived != "false") {
        callback(ErrorResponse::bad_request("invalid_query", "archived must be true or false"));
        return;
    }
    with_repo_errors(callback, "habits.list", [&] {
        callback(Response::ok(json{{"data", HabitRepository().list(owner, archived == "true")}}));
    });
}

void HabitsController::create(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::habits_enabled(), "habits", req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto in = habit_input(body, callback);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "habits.create", [&] {
        callback(Response::created(json{{"data", HabitRepository().create(owner, *in)}}));
    });
}

void HabitsController::update(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::habits_enabled(), "habits", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto in = habit_input(body, callback);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "habits.update", [&] {
        callback(Response::ok(json{{"data", HabitRepository().replace(owner, id, *in)}}));
    });
}

void HabitsController::remove(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::habits_enabled(), "habits", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "habits.delete", [&] {
        HabitRepository().remove(owner, id);
        no_content(callback);
    });
}

void HabitsController::archive(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::habits_enabled(), "habits", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    if (!body.contains("archived") || !body["archived"].is_boolean()) {
        Validation::Errors errs;
        errs.add("archived", "invalid", "must be true or false");
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "habits.archive", [&] {
        callback(Response::ok(json{{"data", HabitRepository().set_archived(owner, id, body["archived"].get<bool>())}}));
    });
}

void HabitsController::days(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::habits_enabled(), "habits", req, callback, owner);
    const std::string from = req->getParameter("from");
    const std::string to = req->getParameter("to");
    if (!is_calendar_date(from) || !is_calendar_date(to) || to < from) {
        callback(ErrorResponse::bad_request("invalid_query", "from and to must be calendar days, from first"));
        return;
    }
    const auto a = ::Habits::day_of(from);
    const auto b = ::Habits::day_of(to);
    if ((b - a).count() >= kRangeMax) {
        callback(ErrorResponse::bad_request("invalid_query", "at most 62 days at a time"));
        return;
    }
    const bool fitness_read = Security::Auth::require_permission(req, Domain::Permission::kFitnessRead) == nullptr;
    with_repo_errors(callback, "habits.days", [&] {
        const auto tz = zone_of(req, callback);
        if (!tz) {
            return;
        }
        const std::string today = HabitRepository().today(*tz);
        const Repositories::Habits::Access access{Core::fitness_enabled() && fitness_read, Core::workout_enabled()};
        json out = json::array();
        for (const auto& e :
             Repositories::Habits::evaluate(owner, a, b, ::Habits::day_of(today), *tz, access, /*history=*/true)) {
            json row = e.habit;
            json list = json::array();
            for (const auto& [day, s] : e.days) {
                list.push_back({{"date", ::Habits::text_of(day)},
                                {"done", s.done},
                                {"manual", s.manual},
                                {"value", s.value ? json(*s.value) : json()}});
            }
            row["days"] = list;
            row["streak"] = e.streak;
            row["best"] = e.best;
            row["week_done"] = e.model.schedule == ::Habits::Schedule::Weekly ? json(e.week_done) : json();
            row["no_data"] = e.no_data;
            out.push_back(std::move(row));
        }
        callback(Response::ok(json{{"data", out}, {"from", from}, {"to", to}, {"today", today}}));
    });
}

void HabitsController::putCheck(const HttpRequestPtr& req,
                                Callback&& callback,
                                const std::string& id,
                                const std::string& date) {
    API_MODULE_USER_GUARD(Core::habits_enabled(), "habits", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body = json::object();
    if (!std::string(req->body()).empty() && !parse_object(req, body, callback)) {
        return;
    }
    std::optional<double> value;
    if (body.contains("value") && !body["value"].is_null()) {
        if (!body["value"].is_number() || !(body["value"].get<double>() >= 0) ||
            body["value"].get<double>() > kGoalMax) {
            Validation::Errors errs;
            errs.add("value", "invalid", "a number from 0 to 1000000");
            callback(Validation::response_400(errs));
            return;
        }
        value = body["value"].get<double>();
    }
    with_repo_errors(callback, "habits.check", [&] {
        const auto tz = zone_of(req, callback);
        if (!tz || !tick_day(date, *tz, callback)) {
            return;
        }
        callback(Response::ok(json{{"data", HabitRepository().put_check(owner, id, date, value)}}));
    });
}

void HabitsController::deleteCheck(const HttpRequestPtr& req,
                                   Callback&& callback,
                                   const std::string& id,
                                   const std::string& date) {
    API_MODULE_USER_GUARD(Core::habits_enabled(), "habits", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    if (!is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_date", "the date must be a calendar day as YYYY-MM-DD"));
        return;
    }
    with_repo_errors(callback, "habits.uncheck", [&] {
        HabitRepository().remove_check(owner, id, date);
        no_content(callback);
    });
}

}  // namespace Api
