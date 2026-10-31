/**
 * @file NowDayController.cpp
 * @brief Body for src/api/NowDayController.hpp.
 */

#include "api/NowDayController.hpp"

#include <chrono>
#include <exception>
#include <string>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/FieldChecks.hpp"
#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "calendar/Access.hpp"
#include "calendar/Google.hpp"
#include "net/Http.hpp"
#include "repositories/Zones.hpp"
#include "repositories/calendar/LinkRepository.hpp"
#include "repositories/now/DayRepository.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

namespace Api {

namespace {

using json = nlohmann::json;

long long now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

void NowDayController::day(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::now_enabled(), "now", req, callback, owner);
    const std::string date = req->getParameter("date");
    const std::string tz_in = req->getParameter("tz");
    const std::string tz = Repositories::Zones::is_zone(tz_in) ? tz_in : "UTC";
    if (!Fields::is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_date", "date is a day as YYYY-MM-DD"));
        return;
    }
    try {
        json out = Repositories::Now::DayRepository().day(owner, date, tz);
        out["events"] = json::array();
        out["free"] = json::array();
        // The calendar's state, so the ribbon can say why it has no events.
        std::string calendar = "off";
        if (Calendar::settings()) {
            Repositories::Calendar::LinkRepository links;
            const long long from = links.local_instant(date, 0, tz);
            const long long to = links.local_instant(date, 24, tz);
            auto events = Calendar::events(Net::Http::transport(), owner, from, to, tz);
            if (auto* list = std::get_if<std::vector<Calendar::Event>>(&events)) {
                calendar = "ok";
                for (const auto& e : *list) {
                    out["events"].push_back(Calendar::event_view(e));
                }
                const long long a = links.local_instant(date, 8, tz);
                const long long b = links.local_instant(date, 22, tz);
                for (const auto& [x, y] : Calendar::free_slots(*list, a, b, 30)) {
                    out["free"].push_back(
                        {{"start", Calendar::rfc3339(x)}, {"end", Calendar::rfc3339(y)}, {"minutes", (y - x) / 60}});
                }
            } else {
                const auto& p = std::get<Calendar::Problem>(events);
                calendar = p.code == "calendar_not_linked"     ? "not_linked"
                           : p.code == "calendar_disconnected" ? "disconnected"
                                                               : "unavailable";
            }
        }
        out["calendar"] = calendar;
        out["date"] = date;
        callback(Response::ok(json{{"data", out}}));
    } catch (const std::exception& e) {
        spdlog::error("now.day failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void NowDayController::focusStats(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::now_enabled(), "now", req, callback, owner);
    const std::string tz_in = req->getParameter("tz");
    const std::string tz = Repositories::Zones::is_zone(tz_in) ? tz_in : "UTC";
    try {
        callback(Response::ok(json{{"data", Repositories::Now::DayRepository().focus_stats(owner, tz)}}));
    } catch (const std::exception& e) {
        spdlog::error("now.focusStats failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void NowDayController::addFocus(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::now_enabled(), "now", req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const std::string started = body.value("started_at", std::string());
    const std::string ended = body.value("ended_at", std::string());
    const auto a = Calendar::parse_time(started);
    const auto b = Calendar::parse_time(ended);
    const long long now = now_unix();
    if (!a || !b || *b <= *a || *b - *a > 4 * 3600 || *b > now + 120 || *a < now - 2 * 86400) {
        callback(ErrorResponse::bad_request(
            "invalid_time",
            "started_at and ended_at are RFC 3339, end after start, 4 hours at most, in the last 2 days"));
        return;
    }
    const auto m = body.find("minutes");
    const long long span = (*b - *a + 59) / 60;
    const long long minutes = m != body.end() && m->is_number_integer() ? m->get<long long>() : span;
    if (minutes < 1 || minutes > span || minutes > 240) {
        callback(ErrorResponse::bad_request("invalid_minutes", "minutes is 1 up to the session's length"));
        return;
    }
    const std::string label = body.value("label", std::string()).substr(0, 200);
    try {
        const std::string id =
            Repositories::Now::DayRepository().add_focus(owner, started, ended, static_cast<int>(minutes), label);
        callback(Response::created(json{{"data", {{"id", id}}}}));
    } catch (const std::exception& e) {
        spdlog::error("now.focus-sessions failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

}  // namespace Api
