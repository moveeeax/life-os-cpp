/**
 * @file WorkoutController.cpp
 * @brief Bodies for src/api/WorkoutController.hpp — compiled once into app_core.
 */

#include "api/WorkoutController.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <drogon/drogon.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "api/Validation.hpp"
#include "core/Modules.hpp"
#include "domain/Role.hpp"
#include "repositories/workout/ExerciseRepository.hpp"
#include "repositories/workout/RoutineRepository.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

using json = nlohmann::json;

namespace {

constexpr int kDefaultLimit = 50;
constexpr int kMaxLimit = 200;

constexpr std::size_t kNameMax = 120;
constexpr std::size_t kNoteMax = 2000;
constexpr std::size_t kListMax = 30;
constexpr std::size_t kListItemMax = 1000;
constexpr std::size_t kRoutineExercisesMax = 100;

const std::vector<std::string> kTrackingModes = {"weight_reps", "bodyweight_reps", "duration", "distance_duration"};

/// Rows belong to an app user; a static-bearer principal has no user id.
bool require_user(const std::string& owner, const WorkoutController::Callback& callback) {
    if (is_valid_uuid(owner)) {
        return true;
    }
    callback(ErrorResponse::forbidden("no_user_account", "this route needs a user account"));
    return false;
}

/// Optional field: a JSON array of at most kListMax strings. Absent is fine.
void string_list(Validation::Errors& errs, const json& body, const std::string& field) {
    if (!body.contains(field)) {
        return;
    }
    const auto& v = body[field];
    if (!v.is_array() || v.size() > kListMax) {
        errs.add(field, "invalid", "must be an array of at most 30 strings");
        return;
    }
    for (const auto& item : v) {
        if (!item.is_string() || item.get<std::string>().size() > kListItemMax) {
            errs.add(field, "invalid", "must be an array of at most 30 strings");
            return;
        }
    }
}

/// The list as serialized JSON for the repository, or nullopt when absent.
std::optional<std::string> list_json(const json& body, const std::string& field) {
    if (!body.contains(field)) {
        return std::nullopt;
    }
    return body[field].dump();
}

/// Optional integer of one routine exercise within [lo, hi]; null is "unset".
void item_int(
    Validation::Errors& errs, const json& item, std::size_t index, const char* field, long long lo, long long hi) {
    if (!item.contains(field) || item[field].is_null()) {
        return;
    }
    if (!item[field].is_number_integer() || item[field].get<long long>() < lo || item[field].get<long long>() > hi) {
        errs.add("exercises[" + std::to_string(index) + "]." + field,
                 "out_of_range",
                 "must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi));
    }
}

/// Copy an optional integer field into the normalized item, keeping null.
void copy_int(json& out, const json& item, const char* field) {
    out[field] = item.contains(field) && item[field].is_number_integer() ? item[field] : json();
}

}  // namespace

// Guards. Order: module -> permission -> identity. The Guards.hpp macros
// return from the method themselves, so this expands in the method body.
// API_REQUIRE_OWNER declares `owner`, hence no do/while wrapper.
#define WORKOUT_GUARD(req, callback, owner)                                  \
    if (!require_enabled(callback))                                          \
        return;                                                              \
    API_REQUIRE_PERMISSION(req, callback, Domain::Permission::kFitnessRead); \
    API_REQUIRE_OWNER(req, callback, owner);                                 \
    if (!require_user(owner, callback))                                      \
    return

bool WorkoutController::require_enabled(const Callback& callback) {
    if (Core::workout_enabled()) {
        return true;
    }
    callback(ErrorResponse::not_found("workout"));
    return false;
}

// ── exercises ────────────────────────────────────────────────────────────────

void WorkoutController::listExercises(const HttpRequestPtr& req, Callback&& callback) {
    WORKOUT_GUARD(req, callback, owner);

    Repositories::ExerciseRepository::Filter filter;
    filter.q = req->getParameter("q");
    filter.muscle = req->getParameter("muscle");
    filter.equipment = req->getParameter("equipment");
    filter.category = req->getParameter("category");
    filter.source = req->getParameter("source");
    filter.include_archived = req->getParameter("archived") == "true";
    if (filter.q.size() > kNameMax) {
        callback(ErrorResponse::bad_request("invalid_query", "q is too long"));
        return;
    }
    if (!filter.source.empty() && filter.source != "library" && filter.source != "custom") {
        callback(ErrorResponse::bad_request("invalid_source", "source must be library or custom"));
        return;
    }
    const auto page = parse_page_params(req, kDefaultLimit, kMaxLimit);

    with_repo_errors(callback, "workout.listExercises", [&] {
        const auto result = Repositories::ExerciseRepository().list(owner, filter, page.limit, page.offset);
        callback(Response::ok(json{{"data", result.rows}, {"count", result.rows.size()}, {"total", result.total}}));
    });
}

void WorkoutController::getExercise(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    WORKOUT_GUARD(req, callback, owner);

    with_repo_errors(callback, "workout.getExercise", [&] {
        const auto found = Repositories::ExerciseRepository().find(owner, id);
        if (!found) {
            throw Repositories::ExerciseNotFound();
        }
        callback(Response::ok(json{{"data", *found}}));
    });
}

void WorkoutController::createExercise(const HttpRequestPtr& req, Callback&& callback) {
    WORKOUT_GUARD(req, callback, owner);

    json body;
    if (!Validation::parse_body(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "name");
    Validation::string_length(errs, body, "name", 1, kNameMax);
    Validation::require_string(errs, body, "tracking_mode");
    Validation::one_of(errs, body, "tracking_mode", kTrackingModes);
    if (body.contains("category")) {
        Validation::string_length(errs, body, "category", 1, kNameMax);
    }
    if (body.contains("equipment")) {
        Validation::string_length(errs, body, "equipment", 0, kNameMax);
    }
    string_list(errs, body, "primary_muscles");
    string_list(errs, body, "secondary_muscles");
    string_list(errs, body, "instructions");
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }

    Repositories::ExerciseRepository::NewExercise e;
    e.name = body["name"].get<std::string>();
    e.tracking_mode = body["tracking_mode"].get<std::string>();
    e.category = Validation::opt_string(body, "category").value_or("strength");
    e.equipment = Validation::opt_string(body, "equipment").value_or("");
    e.primary_muscles_json = list_json(body, "primary_muscles").value_or("[]");
    e.secondary_muscles_json = list_json(body, "secondary_muscles").value_or("[]");
    e.instructions_json = list_json(body, "instructions").value_or("[]");

    with_repo_errors(callback, "workout.createExercise", [&] {
        callback(Response::created(json{{"data", Repositories::ExerciseRepository().create(owner, e)}}));
    });
}

void WorkoutController::updateExercise(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    WORKOUT_GUARD(req, callback, owner);

    json body;
    if (!Validation::parse_body(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    if (body.contains("name")) {
        Validation::string_length(errs, body, "name", 1, kNameMax);
    }
    if (body.contains("category")) {
        Validation::string_length(errs, body, "category", 1, kNameMax);
    }
    if (body.contains("equipment")) {
        Validation::string_length(errs, body, "equipment", 0, kNameMax);
    }
    if (body.contains("tracking_mode")) {
        Validation::one_of(errs, body, "tracking_mode", kTrackingModes);
    }
    if (body.contains("archived")) {
        Validation::boolean(errs, body, "archived");
    }
    string_list(errs, body, "primary_muscles");
    string_list(errs, body, "secondary_muscles");
    string_list(errs, body, "instructions");
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }

    Repositories::ExerciseRepository::Patch patch;
    patch.name = Validation::opt_string(body, "name");
    patch.category = Validation::opt_string(body, "category");
    patch.equipment = Validation::opt_string(body, "equipment");
    patch.tracking_mode = Validation::opt_string(body, "tracking_mode");
    patch.primary_muscles_json = list_json(body, "primary_muscles");
    patch.secondary_muscles_json = list_json(body, "secondary_muscles");
    patch.instructions_json = list_json(body, "instructions");
    if (body.contains("archived") && body["archived"].is_boolean()) {
        patch.archived = body["archived"].get<bool>();
    }

    with_repo_errors(callback, "workout.updateExercise", [&] {
        callback(Response::ok(json{{"data", Repositories::ExerciseRepository().update(owner, id, patch)}}));
    });
}

// ── routines ─────────────────────────────────────────────────────────────────

void WorkoutController::listRoutines(const HttpRequestPtr& req, Callback&& callback) {
    WORKOUT_GUARD(req, callback, owner);

    with_repo_errors(callback, "workout.listRoutines", [&] {
        const auto rows = Repositories::RoutineRepository().list(owner);
        callback(Response::ok(json{{"data", rows}, {"count", rows.size()}, {"total", rows.size()}}));
    });
}

void WorkoutController::getRoutine(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    WORKOUT_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }

    with_repo_errors(callback, "workout.getRoutine", [&] {
        const auto found = Repositories::RoutineRepository().find(owner, id);
        if (!found) {
            throw Repositories::RoutineNotFound();
        }
        callback(Response::ok(json{{"data", *found}}));
    });
}

void WorkoutController::putRoutine(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    WORKOUT_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }

    json body;
    if (!Validation::parse_body(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "name");
    Validation::string_length(errs, body, "name", 1, kNameMax);
    if (body.contains("note")) {
        Validation::string_length(errs, body, "note", 0, kNoteMax);
    }
    if (body.contains("weekday") && !body["weekday"].is_null()) {
        Validation::int_range(errs, body, "weekday", 1, 7);
    }

    json exercises = json::array();
    if (!body.contains("exercises") || !body["exercises"].is_array()) {
        errs.add("exercises", "required", "must be an array");
    } else if (body["exercises"].size() > kRoutineExercisesMax) {
        errs.add("exercises", "too_many", "at most 100 exercises");
    } else {
        std::size_t index = 0;
        for (const auto& item : body["exercises"]) {
            const std::string at = "exercises[" + std::to_string(index) + "]";
            if (!item.is_object() || !item.contains("exercise_id") || !item["exercise_id"].is_string() ||
                item["exercise_id"].get<std::string>().empty()) {
                errs.add(at + ".exercise_id", "required", "must be a non-empty string");
                ++index;
                continue;
            }
            item_int(errs, item, index, "target_sets", 1, 50);
            item_int(errs, item, index, "target_reps_min", 1, 1000);
            item_int(errs, item, index, "target_reps_max", 1, 1000);
            item_int(errs, item, index, "target_duration_seconds", 1, 86400);
            item_int(errs, item, index, "rest_seconds", 0, 3600);
            if (item.contains("target_reps_min") && item["target_reps_min"].is_number_integer() &&
                item.contains("target_reps_max") && item["target_reps_max"].is_number_integer() &&
                item["target_reps_min"].get<long long>() > item["target_reps_max"].get<long long>()) {
                errs.add(at + ".target_reps_min", "out_of_range", "must not be above target_reps_max");
            }
            if (item.contains("note") &&
                (!item["note"].is_string() || item["note"].get<std::string>().size() > kNoteMax)) {
                errs.add(at + ".note", "invalid", "must be a string of at most 2000 characters");
            }

            // Only the known fields travel on to the repository.
            json clean = json::object();
            clean["exercise_id"] = item["exercise_id"];
            copy_int(clean, item, "target_sets");
            copy_int(clean, item, "target_reps_min");
            copy_int(clean, item, "target_reps_max");
            copy_int(clean, item, "target_duration_seconds");
            copy_int(clean, item, "rest_seconds");
            clean["note"] = item.contains("note") && item["note"].is_string() ? item["note"] : json("");
            exercises.push_back(std::move(clean));
            ++index;
        }
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }

    std::optional<int> weekday;
    if (body.contains("weekday") && body["weekday"].is_number_integer()) {
        weekday = body["weekday"].get<int>();
    }
    const std::string name = body["name"].get<std::string>();
    const std::string note = Validation::opt_string(body, "note").value_or("");

    with_repo_errors(callback, "workout.putRoutine", [&] {
        callback(Response::ok(
            json{{"data", Repositories::RoutineRepository().put(owner, id, name, weekday, note, exercises.dump())}}));
    });
}

void WorkoutController::deleteRoutine(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    WORKOUT_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }

    with_repo_errors(callback, "workout.deleteRoutine", [&] {
        Repositories::RoutineRepository().remove(owner, id);
        callback(Response::ok(json{{"message", "Routine deleted"}}));
    });
}

}  // namespace Api
