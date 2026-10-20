/**
 * @file HabitsController.cpp
 * @brief The /api/v1/habits handlers: validation, the owner scope, the manual
 *        ticks, the days evaluated with the auto values of a series of the
 *        metrics catalog (docs/superpowers/specs/2026-10-14-habits-design.md,
 *        docs/superpowers/specs/2026-10-20-metric-sources-design.md).
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
#include "domain/Role.hpp"
#include "habits/Habits.hpp"
#include "habits/Mood.hpp"
#include "metrics/Catalog.hpp"
#include "repositories/habits/Days.hpp"
#include "repositories/habits/HabitRepository.hpp"
#include "repositories/habits/MoodRepository.hpp"
#include "repositories/metrics/Series.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "security/Auth.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Api::Fields::is_calendar_date;
using Api::Fields::opt_int;
using Api::Fields::text_length;
using Repositories::Habits::HabitRepository;
using Repositories::Habits::MoodRepository;

/// The longest range GET /habits/days answers, in days.
constexpr int kRangeMax = 62;
/// The longest range GET /habits/mood answers, in days: a year and a day.
constexpr int kMoodRangeMax = 366;
/// How far back a tick may go, in days.
constexpr int kTickBackMax = 366;
/// The largest goal: a step count with room to spare.
constexpr double kGoalMax = 1000000;
/// The largest goal of a series in hours (sleep, time in bed).
constexpr double kHoursGoalMax = 24;
/// The largest goal of a money series: a balance in a currency of small units.
constexpr double kMoneyGoalMax = 1e12;

const std::vector<std::string> kSchedules = {"daily", "weekly"};
const std::vector<std::string> kDirections = {"at_least", "at_most"};

/// ?tz=, UTC by default; nullopt (answered) when not an IANA zone.
std::optional<std::string> zone_of(const HttpRequestPtr& req, const HabitsController::Callback& callback) {
    const std::string tz = req->getParameter("tz").empty() ? "UTC" : req->getParameter("tz");
    if (!Repositories::Tasks::TaskRepository::is_timezone(tz)) {
        callback(ErrorResponse::bad_request("invalid_query", "tz must be an IANA zone like Asia/Almaty"));
        return std::nullopt;
    }
    return tz;
}

/// A checked habit body and the series its source names (nullopt for manual).
struct HabitInput {
    HabitRepository::Input in;
    std::optional<::Metrics::Ref> ref;
};

/// A habit body, checked; nullopt (answered) when bad.
std::optional<HabitInput> habit_input(const json& body, const HabitsController::Callback& callback) {
    Validation::Errors errs;
    Validation::require_string(errs, body, "name");
    Validation::require_string(errs, body, "schedule");
    text_length(errs, body, "name", 1, 80);
    text_length(errs, body, "unit", 0, 16);
    Validation::one_of(errs, body, "schedule", kSchedules);
    Validation::one_of(errs, body, "direction", kDirections);
    const std::string schedule = Validation::opt_string(body, "schedule").value_or("");
    // "manual", a catalog id, or one of the five names of 1.33 (stored as its catalog id).
    std::string source = "manual";
    std::optional<::Metrics::Ref> ref;
    if (body.contains("source") && !body["source"].is_null()) {
        if (!body["source"].is_string()) {
            errs.add("source", "invalid", "manual or an id of GET /api/v1/metrics");
        } else {
            source = body["source"].get<std::string>();
            ref = ::Habits::source_of(source);
            if (source != "manual" && !ref) {
                errs.add("source", "invalid", "manual or an id of GET /api/v1/metrics");
            }
        }
    }
    // A workout of 1.33 is any finished session: its goal is dropped, as it was then.
    const bool legacy_workout = source == "workout";
    const std::string direction = Validation::opt_string(body, "direction").value_or("at_least");
    if (!ref && direction == "at_most") {
        errs.add("direction", "not_allowed", "only a habit with a source compares at most");
    }
    const auto per_week = opt_int(body, "per_week");
    if (schedule == "weekly" && (!per_week || *per_week < 1 || *per_week > 7)) {
        errs.add("per_week", "invalid", "a weekly habit needs per_week from 1 to 7");
    }
    if (schedule == "daily" && body.contains("per_week") && !body["per_week"].is_null()) {
        errs.add("per_week", "not_allowed", "only a weekly habit takes per_week");
    }
    std::optional<double> goal;
    const bool has_goal = body.contains("goal") && !body["goal"].is_null();
    if (has_goal) {
        // The goal is in the series' unit: hours for sleep, the currency for money.
        double max = kGoalMax;
        if (ref && ref->def->unit == "h") {
            max = kHoursGoalMax;
        } else if (ref && (ref->def->gate == ::Metrics::Gate::Money || ref->def->gate == ::Metrics::Gate::Practice)) {
            max = kMoneyGoalMax;
        }
        if (!body["goal"].is_number() || !(body["goal"].get<double>() > 0) || body["goal"].get<double>() > max) {
            errs.add("goal", "invalid", "a goal is a number above 0, at most " + json(max).dump());
        } else {
            goal = body["goal"].get<double>();
        }
    }
    if (ref && !ref->def->presence && !has_goal && !legacy_workout) {
        // A sum or an average is there every day: without a goal it would tick nothing.
        errs.add("goal", "missing", "this source needs a goal to compare the day with");
    }
    std::optional<double> target;
    if (body.contains("target") && !body["target"].is_null()) {
        if (ref) {
            errs.add("target", "not_allowed", "only a habit ticked by hand takes a daily target");
        } else if (!body["target"].is_number() || !(body["target"].get<double>() > 0) ||
                   body["target"].get<double>() > kGoalMax) {
            errs.add("target", "invalid", "a target is a number above 0, at most 1000000");
        } else {
            target = body["target"].get<double>();
        }
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return std::nullopt;
    }
    if (legacy_workout) {
        goal.reset();
    }
    return HabitInput{HabitRepository::Input{body["name"].get<std::string>(),
                                             schedule,
                                             schedule == "weekly" ? per_week : std::nullopt,
                                             goal,
                                             Validation::opt_string(body, "unit").value_or(""),
                                             ref ? ::Metrics::canonical(*ref) : std::string("manual"),
                                             target,
                                             direction},
                      ref};
}

/**
 * Whether @p ref names a series that is @p owner's: a habit of theirs ticked by hand
 * and not the habit @p self itself, a currency of their money accounts, a category of
 * theirs. False (answered 400 on source) when not. The module switches, the
 * permission and the Mi link are not asked here: a habit on a series the caller
 * cannot read now shows no_data, it is not refused.
 */
bool source_is_yours(const std::string& owner,
                     const std::optional<::Metrics::Ref>& ref,
                     const std::string& self,
                     const HabitsController::Callback& callback) {
    if (!ref) {
        return true;
    }
    Validation::Errors errs;
    if (ref->def->param == ::Metrics::Param::Habit) {
        const auto habit = HabitRepository().get(owner, ref->habit_id);
        if (ref->habit_id == self) {
            errs.add("source", "self", "a habit cannot read its own ticks");
        } else if (!habit) {
            errs.add("source", "not_yours", "no such habit of yours");
        } else if ((*habit)["source"] != "manual") {
            errs.add("source", "not_manual", "only a habit ticked by hand can be a source");
        }
    } else {
        // Every gate open: only the parameters are asked (Repositories::Metrics::available).
        Repositories::Metrics::Access all;
        all.fitness = all.mi_read = all.workout = all.food = all.habits = all.tasks = all.money = all.practice = true;
        all.xiaomi_id = "-";
        if (Repositories::Metrics::available(owner, *ref, all) == Repositories::Metrics::Why::NotYours) {
            errs.add("source", "not_yours", "the currency or the category is not one of yours");
        }
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return false;
    }
    return true;
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
        if (!source_is_yours(owner, in->ref, "", callback)) {
            return;
        }
        callback(Response::created(json{{"data", HabitRepository().create(owner, in->in)}}));
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
        // The id as the catalog keeps a habit's (lower case), so an upper-case path still meets itself.
        if (!source_is_yours(owner, in->ref, ::Metrics::detail::lower(id), callback)) {
            return;
        }
        callback(Response::ok(json{{"data", HabitRepository().replace(owner, id, in->in)}}));
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
        const auto access =
            Repositories::Metrics::access_of(owner, fitness_read ? Domain::Permission::kFitnessRead : 0U);
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

void HabitsController::addCheck(const HttpRequestPtr& req,
                                Callback&& callback,
                                const std::string& id,
                                const std::string& date) {
    API_MODULE_USER_GUARD(Core::habits_enabled(), "habits", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    if (!body.contains("value") || !body["value"].is_number() || !(body["value"].get<double>() > 0) ||
        body["value"].get<double>() > kGoalMax) {
        Validation::Errors errs;
        errs.add("value", "invalid", "the amount to add: a number above 0, at most 1000000");
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "habits.check_add", [&] {
        const auto tz = zone_of(req, callback);
        if (!tz || !tick_day(date, *tz, callback)) {
            return;
        }
        callback(
            Response::ok(json{{"data", HabitRepository().add_to_check(owner, id, date, body["value"].get<double>())}}));
    });
}

void HabitsController::listMood(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::habits_enabled(), "habits", req, callback, owner);
    const std::string from = req->getParameter("from");
    const std::string to = req->getParameter("to");
    if (!is_calendar_date(from) || !is_calendar_date(to) || to < from) {
        callback(ErrorResponse::bad_request("invalid_query", "from and to must be calendar days, from first"));
        return;
    }
    if ((::Habits::day_of(to) - ::Habits::day_of(from)).count() >= kMoodRangeMax) {
        callback(ErrorResponse::bad_request("invalid_query", "at most 366 days at a time"));
        return;
    }
    with_repo_errors(callback, "habits.mood_list", [&] {
        callback(Response::ok(json{{"data", MoodRepository().list(owner, from, to)}, {"from", from}, {"to", to}}));
    });
}

void HabitsController::putMood(const HttpRequestPtr& req, Callback&& callback, const std::string& date) {
    API_MODULE_USER_GUARD(Core::habits_enabled(), "habits", req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    const auto score = opt_int(body, "score");
    if (!score || *score < ::Habits::Mood::kScoreMin || *score > ::Habits::Mood::kScoreMax) {
        errs.add("score", "invalid", "a whole number from 1 to 5");
    }
    std::optional<std::vector<std::string>> tags = std::vector<std::string>{};
    if (body.contains("tags") && !body["tags"].is_null()) {
        tags = ::Habits::Mood::tags_of(body["tags"]);
        if (!tags) {
            std::string known;
            for (const auto t : ::Habits::Mood::kTags) {
                known += known.empty() ? "" : ", ";
                known += t;
            }
            errs.add("tags", "invalid", "a list of these tags: " + known);
        }
    }
    text_length(errs, body, "note", 0, ::Habits::Mood::kNoteMax);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "habits.mood_put", [&] {
        const auto tz = zone_of(req, callback);
        if (!tz || !tick_day(date, *tz, callback)) {
            return;
        }
        MoodRepository::Input in;
        in.score = *score;
        in.tags = *tags;
        in.note = Validation::opt_string(body, "note").value_or("");
        callback(Response::ok(json{{"data", MoodRepository().put(owner, date, in)}}));
    });
}

void HabitsController::deleteMood(const HttpRequestPtr& req, Callback&& callback, const std::string& date) {
    API_MODULE_USER_GUARD(Core::habits_enabled(), "habits", req, callback, owner);
    if (!is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_date", "the date must be a calendar day as YYYY-MM-DD"));
        return;
    }
    with_repo_errors(callback, "habits.mood_delete", [&] {
        MoodRepository().remove(owner, date);
        no_content(callback);
    });
}

}  // namespace Api
