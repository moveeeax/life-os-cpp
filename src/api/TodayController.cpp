/**
 * @file TodayController.cpp
 * @brief The /api/v1/today handlers: the digest assembled from the sections
 *        (Now::Today) and the focus of the day
 *        (docs/superpowers/specs/2026-10-12-now-shell-design.md §2).
 */

#include "api/TodayController.hpp"

#include <chrono>
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
#include "core/Modules.hpp"
#include "domain/Role.hpp"
#include "goals/Progress.hpp"
#include "money/Period.hpp"
#include "net/Http.hpp"
#include "now/Settings.hpp"
#include "now/Today.hpp"
#include "now/Weather.hpp"
#include "repositories/fitness/HealthReadRepository.hpp"
#include "repositories/fitness/MiAccountRepository.hpp"
#include "repositories/food/EntryRepository.hpp"
#include "repositories/food/GoalsBody.hpp"
#include "repositories/food/GoalsRepository.hpp"
#include "repositories/goals/GoalRepository.hpp"
#include "repositories/money/ReportBuilder.hpp"
#include "repositories/money/SummaryRepository.hpp"
#include "repositories/money/TransactionRepository.hpp"
#include "repositories/now/FocusRepository.hpp"
#include "repositories/now/NoteRepository.hpp"
#include "repositories/now/SettingsRepository.hpp"
#include "repositories/tasks/NoteRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "repositories/travel/Counters.hpp"
#include "repositories/travel/TripRepository.hpp"
#include "security/Auth.hpp"
#include "tasks/Agenda.hpp"
#include "utils/Date.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Api::Fields::is_calendar_date;
using Api::Fields::today_utc;
using Repositories::Goals::GoalRepository;
using Repositories::Now::FocusRepository;
using Repositories::Tasks::NoteRepository;
using Repositories::Tasks::TaskRepository;

constexpr std::size_t kTextMax = 200;
constexpr std::size_t kNoteMax = 2000;
constexpr std::size_t kCityMax = 120;
const std::vector<std::string> kKinds = {"task", "goal", "text"};

/// The now switch, then a user account; false (answered) otherwise.
bool require_user(const std::string& owner, const TodayController::Callback& callback) {
    if (is_valid_uuid(owner)) {
        return true;
    }
    callback(ErrorResponse::forbidden("no_user_account", "this route needs a user account"));
    return false;
}

#define TODAY_GUARD(req, callback, owner)          \
    if (!Core::now_enabled()) {                    \
        callback(ErrorResponse::not_found("now")); \
        return;                                    \
    }                                              \
    API_REQUIRE_OWNER(req, callback, owner);       \
    if (!require_user(owner, callback))            \
    return

bool parse_object(const HttpRequestPtr& req, json& body, TodayController::Callback& callback) {
    if (!Validation::parse_body(req, body, callback)) {
        return false;
    }
    if (!body.is_object()) {
        callback(ErrorResponse::bad_request("invalid_body", "the body must be a JSON object"));
        return false;
    }
    return true;
}

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

json goals_with_progress(const std::string& owner, const std::string& date) {
    GoalRepository repo;
    const auto counts = repo.task_counts(owner);
    const auto points = repo.checkin_points_all(owner);
    json out = json::array();
    for (auto row : repo.list(owner, "active")) {
        const std::string id = row["id"].get<std::string>();
        const auto goal = GoalRepository::progress_goal(row);
        const auto c = counts.find(id);
        const auto p = points.find(id);
        row["progress"] = ::Goals::Progress::compute(
            goal,
            date,
            p == points.end() || goal.kind != "number" ? std::vector<::Goals::Progress::Checkin>{} : p->second,
            c == counts.end() ? ::Goals::Progress::Counts{0, 0} : c->second);
        out.push_back(std::move(row));
    }
    return out;
}

json food_part(const std::string& owner, const std::string& date) {
    const json totals = Repositories::EntryRepository().day(owner, date)["totals"];
    const json targets = Repositories::food_goals_body(owner, date)["targets"];
    const double kcal = totals.value("kcal", 0.0);
    if (kcal == 0.0 && totals.value("protein_g", 0.0) == 0.0 && targets.is_null()) {
        return json();
    }
    json eaten = json::object();
    json target = targets.is_null() ? json() : json::object();
    for (const char* k : {"kcal", "protein_g", "fat_g", "carbs_g"}) {
        eaten[k] = totals.value(k, 0.0);
        if (!targets.is_null()) {
            target[k] = targets.contains(k) ? targets[k] : json();
        }
    }
    return json{{"eaten", eaten}, {"target", target}};
}

json money_part(const std::string& owner, const std::string& date) {
    using ::Money::Period::Kind;
    Repositories::Money::TransactionRepository tx;
    const json rows = tx.report_rows(owner, date, date);
    const json report = Repositories::Money::ReportBuilder::build(
        owner, Kind::month, "month", ::Money::Period::of(Kind::month, date), date, "");
    const json currencies = ::Now::Today::money_block(rows, report);
    Repositories::Money::TransactionRepository::Filter f;
    f.from = date;
    f.to = date;
    f.type = "expense";
    f.status = "posted";
    f.limit = static_cast<long>(::Now::Today::kSpendsShown);
    const json spends = ::Now::Today::spends_block(tx.list(owner, f).rows);
    // All accounts in USD with the change over 30 days, for the corners and outside dashboards.
    json net_worth;
    if (const auto s = Repositories::Money::summary_json(owner, "USD", 30, date)) {
        if (!(*s)["accounts"].empty() || !(*s)["not_converted"].empty()) {
            net_worth = json{{"currency", "USD"},
                             {"total", (*s)["total"]},
                             {"partial", (*s)["partial"]},
                             {"rate_date", (*s)["rate_date"]},
                             {"change_30d", (*s)["change"]["total"]},
                             {"change_partial", (*s)["change"]["partial"]}};
        }
    }
    if (currencies.is_null() && spends.empty() && net_worth.is_null()) {
        return json();
    }
    return json{{"currencies", currencies.is_null() ? json::array() : currencies},
                {"spends_today", spends},
                {"net_worth", net_worth}};
}

json health_part(const std::string& owner, const std::string& date) {
    const std::string xiaomi = Repositories::MiAccountRepository("").xiaomi_id_of(owner).value_or("");
    json summary_row;
    json sleep_rows = json::array();
    json body_rows = json::array();
    if (!xiaomi.empty()) {
        Repositories::HealthReadRepository health(xiaomi);
        const long long offset = Repositories::region_offset_seconds(owner);
        const auto summary = health.summary(date, date, 1, 0, offset).rows;
        if (!summary.empty()) {
            summary_row = summary[0];
        }
        const auto day = std::chrono::sys_days{Utils::Date::parse_ymd(date)};
        // Sleep rows are filed by the UTC day of their end; the account's day may start hours earlier.
        sleep_rows = ::Now::Today::sleep_on_local_day(
            health.sleep(::Money::Period::detail::text_of(day - std::chrono::days{1}), date, 20, 0).rows, date, offset);
        body_rows = health.body(::Money::Period::detail::text_of(day - std::chrono::days{8}), date, 50, 0).rows;
    }
    if (body_rows.empty()) {
        // No weighing synced from the scale: the latest weight the food profile knows, if any.
        if (const auto weight = Repositories::GoalsRepository().scale_weight(owner)) {
            body_rows.push_back(json{{"timestamp", date + "T00:00:00Z"}, {"weight_kg", *weight}});
        }
    }
    return ::Now::Today::health_block(summary_row, sleep_rows, body_rows);
}

}  // namespace

void TodayController::today(const HttpRequestPtr& req, Callback&& callback) {
    TODAY_GUARD(req, callback, owner);
    const std::string date = req->getParameter("date").empty() ? today_utc() : req->getParameter("date");
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
        const std::vector<::Now::Today::Part> parts = {
            {"tasks",
             Core::tasks_enabled(),
             [&] {
                 const auto groups = ::Tasks::Agenda::group(TaskRepository().agenda_items(owner, date, tz), date);
                 return ::Now::Today::tasks_block(
                     groups, date, static_cast<long>(NoteRepository().list(owner, "inbox").size()));
             }},
            {"goals",
             Core::goals_enabled(),
             [&] { return ::Now::Today::goals_block(goals_with_progress(owner, date)); }},
            {"food", Core::food_enabled(), [&] { return food_part(owner, date); }},
            {"money", Core::money_enabled(), [&] { return money_part(owner, date); }},
            {"health", Core::fitness_enabled() && fitness_read, [&] { return health_part(owner, date); }},
            {"note",
             true,
             [&] {
                 const auto [today_note, yesterday_note] =
                     Repositories::Now::NoteRepository().with_yesterday(owner, date);
                 return json{{"today", today_note ? json(*today_note) : json()},
                             {"yesterday", yesterday_note ? json(*yesterday_note) : json()}};
             }},
            {"travel",
             Core::travel_enabled(),
             [&] {
                 return ::Now::Today::travel_block(
                     Repositories::Travel::TripRepository().next(owner, date).value_or(json()),
                     Repositories::Travel::counters_json(owner, date),
                     date);
             }},
        };
        const auto focus = FocusRepository().get(owner, date);
        json data = ::Now::Today::assemble(parts, focus ? focus_out(owner, *focus) : json());
        callback(Response::ok(json{{"data", data}}));
    });
}

void TodayController::putFocus(const HttpRequestPtr& req, Callback&& callback) {
    TODAY_GUARD(req, callback, owner);
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
    TODAY_GUARD(req, callback, owner);
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
                TaskRepository tasks;
                const auto task = tasks.get(owner, ref);
                if (!task) {  // deleted since the check above
                    callback(ErrorResponse::unprocessable("focus_ref_not_found", "the task of this focus is gone"));
                    return;
                }
                const bool closed = (*task)["status"].get<std::string>() == "done";
                if (closed != done) {
                    TaskRepository::Patch p{};
                    p.status = done ? "done" : "open";
                    tasks.update(owner, ref, p);
                }
            }
        }
        callback(Response::ok(json{{"data", focus_out(owner, focus.set_done(owner, date, done))}}));
    });
}

void TodayController::deleteFocus(const HttpRequestPtr& req, Callback&& callback) {
    TODAY_GUARD(req, callback, owner);
    const std::string date = req->getParameter("date");
    if (!is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_query", "date must be a calendar day"));
        return;
    }
    with_repo_errors(callback, "today.deleteFocus", [&] {
        if (!FocusRepository().remove(owner, date)) {
            throw Repositories::Now::FocusNotFound();
        }
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k204NoContent);
        callback(resp);
    });
}

// ── the note of a day ───────────────────────────────────────────────────────

void TodayController::putNote(const HttpRequestPtr& req, Callback&& callback) {
    TODAY_GUARD(req, callback, owner);
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
    TODAY_GUARD(req, callback, owner);
    const std::string date = req->getParameter("date");
    if (!is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_query", "date must be a calendar day"));
        return;
    }
    with_repo_errors(callback, "today.deleteNote", [&] {
        Repositories::Now::NoteRepository().remove(owner, date);
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k204NoContent);
        callback(resp);
    });
}

// ── settings and weather ────────────────────────────────────────────────────

void TodayController::getSettings(const HttpRequestPtr& req, Callback&& callback) {
    TODAY_GUARD(req, callback, owner);
    with_repo_errors(callback, "now.settings", [&] {
        callback(Response::ok(json{{"data", Repositories::Now::SettingsRepository().get(owner)}}));
    });
}

void TodayController::putSettings(const HttpRequestPtr& req, Callback&& callback) {
    TODAY_GUARD(req, callback, owner);
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
                const auto found = ::Now::Weather::geocode(Net::Http::transport(), body["city"].get<std::string>());
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
        callback(Response::ok(json{{"data", repo.get(owner)}}));
    });
}

void TodayController::weather(const HttpRequestPtr& req, Callback&& callback) {
    TODAY_GUARD(req, callback, owner);
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
    try {
        const json block = ::Now::Weather::forecast(Net::Http::transport(), lat, lon, place);
        if (Cache::is_initialized()) {
            Cache::get().set(key, block.dump(), ::Now::Weather::kCacheSeconds);
        }
        callback(Response::ok(json{{"data", block}, {"cached", false}}));
    } catch (const ::Now::Weather::Unavailable& e) {
        spdlog::warn("now: the weather is unavailable: {}", e.what());
        callback(ErrorResponse::service_unavailable("weather_unavailable", "the weather service is not answering"));
    }
}

}  // namespace Api
