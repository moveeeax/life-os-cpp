/**
 * @file NowDayController.cpp
 * @brief Body for src/api/NowDayController.hpp.
 */

#include "api/NowDayController.hpp"

#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/CalendarCache.hpp"
#include "api/FieldChecks.hpp"
#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "calendar/Access.hpp"
#include "calendar/Google.hpp"
#include "net/Http.hpp"
#include "repositories/Zones.hpp"
#include "repositories/calendar/LinkRepository.hpp"
#include "repositories/now/DayRepository.hpp"
#include "utils/BlockingPool.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"
#include "utils/Utf8.hpp"

namespace Api {

namespace {

using json = nlohmann::json;

long long now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// ?tz=: UTC when absent, else an IANA zone; nullopt (answered 400) when it names none,
/// as /today does, so a typo does not move the day silently.
std::optional<std::string> zone_of(const HttpRequestPtr& req, const NowDayController::Callback& callback) {
    const std::string tz = req->getParameter("tz");
    if (tz.empty()) {
        return std::string("UTC");
    }
    if (!Repositories::Zones::is_zone(tz)) {
        callback(ErrorResponse::bad_request("invalid_query", "tz must be an IANA zone like Asia/Almaty"));
        return std::nullopt;
    }
    return tz;
}

/// One Google call of the page path at most this long (a token refresh and the events: two calls).
constexpr long kPageCallSeconds = 4;

/// The day in unix seconds: [from, to) for the events, [a, b) (08:00 to 22:00) for the free slots.
struct DaySpan {
    long long from = 0;
    long long to = 0;
    long long a = 0;
    long long b = 0;
};

void with_events(json& out, const std::vector<Calendar::Event>& list, const DaySpan& span) {
    out["calendar"] = "ok";
    for (const auto& e : list) {
        out["events"].push_back(Calendar::event_view(e));
    }
    for (const auto& [x, y] : Calendar::free_slots(list, span.a, span.b, 30)) {
        out["free"].push_back(
            {{"start", Calendar::rfc3339(x)}, {"end", Calendar::rfc3339(y)}, {"minutes", (y - x) / 60}});
    }
}

}  // namespace

void NowDayController::day(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::now_enabled(), "now", req, callback, owner);
    const std::string date = req->getParameter("date");
    const auto zone = zone_of(req, callback);
    if (!zone) {
        return;
    }
    const std::string& tz = *zone;
    if (!Fields::is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_date", "date is a day as YYYY-MM-DD"));
        return;
    }
    json out;
    DaySpan span;
    try {
        out = Repositories::Now::DayRepository().day(owner, date, tz);
        out["events"] = json::array();
        out["free"] = json::array();
        out["date"] = date;
        // The calendar's state, so the ribbon can say why it has no events.
        out["calendar"] = "off";
        if (!Calendar::settings()) {
            callback(Response::ok(json{{"data", out}}));
            return;
        }
        Repositories::Calendar::LinkRepository links;
        // Most users have no calendar linked: that answer needs one row, not the pool.
        const auto link = links.get(owner);
        if (!link || link->broken) {
            out["calendar"] = link ? "disconnected" : "not_linked";
            callback(Response::ok(json{{"data", out}}));
            return;
        }
        span = DaySpan{links.local_instant(date, 0, tz),
                       links.local_instant(date, 24, tz),
                       links.local_instant(date, 8, tz),
                       links.local_instant(date, 22, tz)};
        if (const auto kept = CalendarCache::get(owner, span.from, span.to)) {
            with_events(out, *kept, span);
            callback(Response::ok(json{{"data", out}}));
            return;
        }
        if (CalendarCache::down(owner)) {
            // Google failed a moment ago: the day comes without events instead of waiting on it again.
            out["calendar"] = "unavailable";
            callback(Response::ok(json{{"data", out}}));
            return;
        }
    } catch (const std::exception& e) {
        spdlog::error("now.day failed: {}", e.what());
        callback(ErrorResponse::internal_error());
        return;
    }
    // Google is asked off the IO thread, each call cut to a few seconds: the page waits on it.
    auto respond = std::make_shared<Callback>(std::move(callback));
    const bool queued = Utils::BlockingPool::outbound().try_submit([respond, out, owner, tz, span]() mutable {
        try {
            Net::Http::CappedTransport quick(Net::Http::transport(), kPageCallSeconds);
            auto events = Calendar::events(quick, owner, span.from, span.to, tz);
            if (auto* list = std::get_if<std::vector<Calendar::Event>>(&events)) {
                CalendarCache::put(owner, span.from, span.to, *list);
                with_events(out, *list, span);
            } else {
                const auto& p = std::get<Calendar::Problem>(events);
                out["calendar"] = p.code == "calendar_not_linked"     ? "not_linked"
                                  : p.code == "calendar_disconnected" ? "disconnected"
                                                                      : "unavailable";
                if (p.status == 502) {
                    CalendarCache::mark_down(owner);
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("now.day: the calendar part failed: {}", e.what());
            out["calendar"] = "unavailable";
        }
        (*respond)(Response::ok(json{{"data", out}}));
    });
    if (!queued) {
        out["calendar"] = "unavailable";
        (*respond)(Response::ok(json{{"data", out}}));
    }
}

void NowDayController::focusStats(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::now_enabled(), "now", req, callback, owner);
    const auto zone = zone_of(req, callback);
    if (!zone) {
        return;
    }
    const std::string& tz = *zone;
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
    // Not a string is the same bad time as a bad string: 400, never a type_error.
    const std::string started = Validation::opt_string(body, "started_at").value_or("");
    const std::string ended = Validation::opt_string(body, "ended_at").value_or("");
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
    // Null or absent is no label; 200 characters, never half a character (invalid UTF-8 fails the insert).
    const auto label_in = Fields::text_or_empty(body, "label");
    if (!label_in) {
        callback(ErrorResponse::bad_request("invalid_label", "label is text"));
        return;
    }
    const std::string label = Utils::Utf8::cut(*label_in, 200);
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
