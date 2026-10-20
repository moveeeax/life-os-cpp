/**
 * @file TodayController.cpp
 * @brief The /api/v1/today handlers: the digest assembled (Now::Today) from
 *        the parts each module hands over through its digest facade
 *        (src/<module>/now/Digest.hpp), and the focus of the day
 *        (docs/superpowers/specs/2026-10-12-now-shell-design.md §2). The
 *        controller composes no module's repositories itself.
 */

#include "api/TodayController.hpp"

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
#include "cache/Cache.hpp"
#include "domain/Role.hpp"
#include "fitness/now/Digest.hpp"
#include "food/now/Digest.hpp"
#include "goals/now/Digest.hpp"
#include "habits/now/Digest.hpp"
#include "money/now/Digest.hpp"
#include "net/Http.hpp"
#include "now/Settings.hpp"
#include "now/Today.hpp"
#include "now/Weather.hpp"
#include "repositories/Zones.hpp"
#include "repositories/goals/GoalRepository.hpp"
#include "repositories/metrics/Series.hpp"
#include "repositories/now/FocusRepository.hpp"
#include "repositories/now/NoteRepository.hpp"
#include "repositories/now/SettingsRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "security/Auth.hpp"
#include "tasks/now/Digest.hpp"
#include "travel/now/Digest.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Api::Fields::is_calendar_date;
using Api::Fields::today_utc;
using Repositories::Goals::GoalRepository;
using Repositories::Now::FocusRepository;
using Repositories::Tasks::TaskRepository;

constexpr std::size_t kTextMax = 200;
constexpr std::size_t kNoteMax = 2000;
constexpr std::size_t kCityMax = 120;
const std::vector<std::string> kKinds = {"task", "goal", "text"};

/// The date of a body: a calendar day, required.
bool body_date(Validation::Errors& errs, const json& body) {
    if (!body.contains("date") || !body["date"].is_string() || !is_calendar_date(body["date"].get<std::string>())) {
        errs.add("date", "invalid", "date must be a calendar day as YYYY-MM-DD");
        return false;
    }
    return true;
}

/// Whether the task or goal a focus points at is still the caller's.
bool ref_exists(const std::string& owner, const std::string& kind, const std::string& ref) {
    if (kind == "task") {
        return TaskRepository().get(owner, ref).has_value();
    }
    if (kind == "goal") {
        return GoalRepository().kind_of(owner, ref).has_value();
    }
    return true;
}

/// The stored focus with "missing" for a task or goal that is gone.
json focus_out(const std::string& owner, json row) {
    const bool has_ref = row["ref_id"].is_string();
    row["missing"] = has_ref && !ref_exists(owner, row["kind"].get<std::string>(), row["ref_id"].get<std::string>());
    return row;
}

}  // namespace

void TodayController::today(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::now_enabled(), "now", req, callback, owner);
    const std::string date = request_day(req, &Repositories::Zones::today_in, today_utc());
    if (!is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_query", "date must be a calendar day"));
        return;
    }
    const std::string tz = req->getParameter("tz").empty() ? "UTC" : req->getParameter("tz");
    const bool fitness_read = Security::Auth::require_permission(req, Domain::Permission::kFitnessRead) == nullptr;
    with_repo_errors(callback, "today.get", [&] {
        if (!TaskRepository::is_timezone(tz)) {
            callback(ErrorResponse::bad_request("invalid_query", "tz must be an IANA zone like Asia/Almaty"));
            return;
        }
        // Each module hands its part over through its digest facade; a part
        // that throws lands in "errors" (Now::Today::assemble), never fails the whole.
        const std::vector<::Now::Today::Part> parts = {
            {"tasks", Core::tasks_enabled(), [&] { return ::Tasks::digest(owner, date, tz); }},
            {"goals", Core::goals_enabled(), [&] { return ::Goals::digest(owner, date); }},
            {"food", Core::food_enabled(), [&] { return ::Food::digest(owner, date); }},
            {"money", Core::money_enabled(), [&] { return ::Money::digest(owner, date); }},
            {"health", Core::fitness_enabled() && fitness_read, [&] { return ::Fitness::digest(owner, date); }},
            {"travel", Core::travel_enabled(), [&] { return ::Travel::digest(owner, date); }},
            {"habits",
             Core::habits_enabled(),
             [&] {
                 return ::Habits::digest(
                     owner,
                     date,
                     tz,
                     Repositories::Metrics::access_of(owner, fitness_read ? Domain::Permission::kFitnessRead : 0U));
             }},
        };
        const auto focus = FocusRepository().get(owner, date);
        json data = ::Now::Today::assemble(parts, focus ? focus_out(owner, *focus) : json());
        callback(Response::ok(json{{"data", data}}));
    });
}

void TodayController::putFocus(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::now_enabled(), "now", req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    body_date(errs, body);
    Validation::require(errs, body, "kind");
    Validation::one_of(errs, body, "kind", kKinds);
    const std::string kind = body.contains("kind") && body["kind"].is_string() ? body["kind"].get<std::string>() : "";
    if (kind == "task" || kind == "goal") {
        Validation::require(errs, body, "ref_id");
        Validation::uuid(errs, body, "ref_id");
    } else if (kind == "text") {
        if (body.contains("ref_id") && !body["ref_id"].is_null()) {
            errs.add("ref_id", "not_allowed", "a text focus points at nothing");
        }
        Validation::require(errs, body, "text");
        Api::Fields::text_length(errs, body, "text", 1, kTextMax);
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    const std::string date = body["date"].get<std::string>();
    with_repo_errors(callback, "today.putFocus", [&] {
        std::optional<std::string> ref;
        std::string text;
        if (kind == "text") {
            text = body["text"].get<std::string>();
        } else {
            ref = body["ref_id"].get<std::string>();
            std::optional<json> row;
            if (kind == "task") {
                row = TaskRepository().get(owner, *ref);
            } else {
                row = GoalRepository().get(owner, *ref);
            }
            if (!row) {
                callback(ErrorResponse::unprocessable("focus_ref_not_found", "no such " + kind + " of yours"));
                return;
            }
            // Task and goal titles are capped at kTextMax characters by their own CHECK.
            text = (*row)["title"].get<std::string>();
        }
        const json saved = FocusRepository().put(owner, date, kind, ref, text);
        callback(Response::ok(json{{"data", focus_out(owner, saved)}}));
    });
}

void TodayController::focusDone(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::now_enabled(), "now", req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    body_date(errs, body);
    Validation::require(errs, body, "done");
    Validation::boolean(errs, body, "done");
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    const std::string date = body["date"].get<std::string>();
    const bool done = body["done"].get<bool>();
    with_repo_errors(callback, "today.focusDone", [&] {
        FocusRepository focus;
        const auto current = focus.get(owner, date);
        if (!current) {
            throw Repositories::Now::FocusNotFound();
        }
        const std::string kind = (*current)["kind"].get<std::string>();
        if ((*current)["ref_id"].is_string()) {
            const std::string ref = (*current)["ref_id"].get<std::string>();
            if (!ref_exists(owner, kind, ref)) {
                callback(ErrorResponse::unprocessable("focus_ref_not_found", "the " + kind + " of this focus is gone"));
                return;
            }
            if (kind == "task") {
                // The task closes and reopens with the focus; a task already in
                // that state is left alone (closed elsewhere, ticked twice).
                const auto task = TaskRepository().get(owner, ref);
                if (!task) {  // deleted since the check above
                    callback(ErrorResponse::unprocessable("focus_ref_not_found", "the task of this focus is gone"));
                    return;
                }
                const bool closed = (*task)["status"].get<std::string>() == "done";
                if (closed != done) {
                    TaskRepository::Patch p{};
                    p.status = done ? "done" : "open";
                    p.today = date;  // a repeating task counts its next occurrence from the focus day
                    // One transaction: a focus ticked without its task (or the
                    // other way round) is a lie the Now screen would show.
                    const json ticked = Repositories::Tasks::detail::translate_sql(
                        [&] {
                            return Database::get().execute_write([&](auto& txn) {
                                TaskRepository::update_in(txn, owner, ref, p);
                                return FocusRepository::set_done_in(txn, owner, date, done);
                            });
                        },
                        &Repositories::Tasks::detail::translate);
                    callback(Response::ok(json{{"data", focus_out(owner, ticked)}}));
                    return;
                }
            }
        }
        callback(Response::ok(json{{"data", focus_out(owner, focus.set_done(owner, date, done))}}));
    });
}

void TodayController::deleteFocus(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::now_enabled(), "now", req, callback, owner);
    const std::string date = req->getParameter("date");
    if (!is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_query", "date must be a calendar day"));
        return;
    }
    with_repo_errors(callback, "today.deleteFocus", [&] {
        if (!FocusRepository().remove(owner, date)) {
            throw Repositories::Now::FocusNotFound();
        }
        no_content(callback);
    });
}

// ── the note of a day ───────────────────────────────────────────────────────

void TodayController::putNote(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::now_enabled(), "now", req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    body_date(errs, body);
    Validation::require(errs, body, "text");
    Api::Fields::text_length(errs, body, "text", 1, kNoteMax);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    const std::string date = body["date"].get<std::string>();
    const std::string text = body["text"].get<std::string>();
    with_repo_errors(callback, "today.putNote", [&] {
        Repositories::Now::NoteRepository().put(owner, date, text);
        callback(Response::ok(json{{"data", {{"date", date}, {"text", text}}}}));
    });
}

void TodayController::deleteNote(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::now_enabled(), "now", req, callback, owner);
    const std::string date = req->getParameter("date");
    if (!is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_query", "date must be a calendar day"));
        return;
    }
    with_repo_errors(callback, "today.deleteNote", [&] {
        Repositories::Now::NoteRepository().remove(owner, date);
        no_content(callback);
    });
}

// ── settings and weather ────────────────────────────────────────────────────

void TodayController::getSettings(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::now_enabled(), "now", req, callback, owner);
    with_repo_errors(callback, "now.settings", [&] {
        callback(Response::ok(json{{"data", Repositories::Now::SettingsRepository().get(owner)}}));
    });
}

void TodayController::putSettings(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::now_enabled(), "now", req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    if (body.contains("city") && !body["city"].is_null()) {
        Api::Fields::text_length(errs, body, "city", 1, kCityMax);
    }
    if (body.contains("corners")) {
        if (const auto problem = ::Now::Settings::corners_problem(body["corners"])) {
            errs.add("corners", "invalid", *problem);
        }
    }
    json links;
    if (body.contains("links")) {
        if (const auto problem = ::Now::Settings::links_problem(body["links"], links)) {
            errs.add("links", "invalid", *problem);
        }
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    // The geocoder first, outside any transaction: a city it does not know saves nothing.
    std::optional<std::optional<::Now::Weather::Place>> place;
    if (body.contains("city")) {
        if (body["city"].is_null()) {
            place = std::optional<::Now::Weather::Place>();
        } else {
            try {
                const auto found =
                    ::Now::Weather::geocode(Net::Http::quick_transport(), body["city"].get<std::string>());
                if (!found) {
                    callback(ErrorResponse::unprocessable("city_not_found", "the geocoder knows no such city"));
                    return;
                }
                place = found;
            } catch (const ::Now::Weather::Unavailable& e) {
                spdlog::warn("now: the geocoder is unavailable: {}", e.what());
                callback(
                    ErrorResponse::service_unavailable("geocoder_unavailable", "the city lookup is not answering"));
                return;
            }
        }
    }
    with_repo_errors(callback, "now.putSettings", [&] {
        Repositories::Now::SettingsRepository repo;
        if (place) {
            repo.put_place(owner, *place);
        }
        if (body.contains("corners")) {
            repo.put_corners(owner, body["corners"]);
        }
        if (body.contains("links")) {
            repo.put_links(owner, links);
        }
        callback(Response::ok(json{{"data", repo.get(owner)}}));
    });
}

void TodayController::weather(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::now_enabled(), "now", req, callback, owner);
    json settings;
    try {
        settings = Repositories::Now::SettingsRepository().get(owner);
    } catch (const std::exception& e) {
        spdlog::error("now.weather: {}", e.what());
        callback(ErrorResponse::internal_error());
        return;
    }
    if (settings["latitude"].is_null() || settings["longitude"].is_null()) {
        callback(Response::ok(json{{"data", nullptr}}));
        return;
    }
    const double lat = settings["latitude"].get<double>();
    const double lon = settings["longitude"].get<double>();
    const std::string place = settings["city"].get<std::string>();
    // Weather is public: one cached answer per coordinates serves every owner there.
    const std::string key = ::Now::Weather::cache_key(lat, lon);
    if (Cache::is_initialized()) {
        if (const auto cached = Cache::get().get(key)) {
            json block = json::parse(*cached, nullptr, /*allow_exceptions=*/false);
            if (block.is_object()) {
                block["place"] = place;
                callback(Response::ok(json{{"data", block}, {"cached", true}}));
                return;
            }
        }
    }
    // A call that failed a moment ago is not repeated: each one could hold a server thread for seconds.
    const std::string down = ::Now::Weather::down_key(lat, lon);
    if (Cache::is_initialized() && Cache::get().exists(down)) {
        callback(ErrorResponse::service_unavailable("weather_unavailable", "the weather service is not answering"));
        return;
    }
    try {
        const json block = ::Now::Weather::forecast(Net::Http::quick_transport(), lat, lon, place);
        if (Cache::is_initialized()) {
            Cache::get().set(key, block.dump(), ::Now::Weather::kCacheSeconds);
        }
        callback(Response::ok(json{{"data", block}, {"cached", false}}));
    } catch (const ::Now::Weather::Unavailable& e) {
        spdlog::warn("now: the weather is unavailable: {}", e.what());
        if (Cache::is_initialized()) {
            Cache::get().set(down, "1", ::Now::Weather::kDownSeconds);
        }
        callback(ErrorResponse::service_unavailable("weather_unavailable", "the weather service is not answering"));
    }
}

}  // namespace Api
