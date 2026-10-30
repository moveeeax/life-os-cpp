/**
 * @file CalendarController.cpp
 * @brief Body for src/api/CalendarController.hpp.
 */

#include "api/CalendarController.hpp"

#include <algorithm>
#include <ctime>
#include <exception>
#include <optional>
#include <regex>
#include <vector>

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
#include "security/Audit.hpp"
#include "security/Auth.hpp"
#include "security/SessionCookies.hpp"
#include "security/SessionStore.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Seal.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Repositories::Calendar::LinkRepository;

constexpr int kMaxRangeDays = 31;

drogon::HttpResponsePtr problem(const Calendar::Problem& p) {
    if (p.status == 404) {
        return ErrorResponse::not_found(p.code);
    }
    if (p.status == 409) {
        return ErrorResponse::conflict(p.code, "Google refused the calendar: connect it again in Account");
    }
    return ErrorResponse::make({drogon::k502BadGateway, p.code, "Google did not answer"});
}

std::string tz_of(const HttpRequestPtr& req) {
    const std::string tz = req->getParameter("tz");
    return Repositories::Zones::is_zone(tz) ? tz : "UTC";
}

bool event_id_ok(const std::string& id) {
    static const std::regex kId("^[A-Za-z0-9_-]{1,1024}$");
    return std::regex_match(id, kId);
}

/// {start, end} of a body as RFC 3339 instants, end after start, at most 24 hours; a 400 otherwise.
std::optional<std::pair<std::string, std::string>> span_of(const json& body,
                                                           const CalendarController::Callback& callback) {
    const auto text = [&](const char* k) {
        return body.contains(k) && body[k].is_string() ? body[k].get<std::string>() : std::string();
    };
    const std::string start = text("start");
    const std::string end = text("end");
    const auto a = Calendar::parse_time(start);
    const auto b = Calendar::parse_time(end);
    if (!a || !b || *b <= *a || *b - *a > 24 * 3600) {
        callback(
            ErrorResponse::bad_request("invalid_time", "start and end are RFC 3339, end after start, a day at most"));
        return std::nullopt;
    }
    return std::make_pair(start, end);
}

/// The user signed in on this browser (the access cookie; SameSite=Lax rides the top-level return from
/// Google), or "" when there is none. The callback path is public, so the middleware did not resolve it.
std::string session_of(const HttpRequestPtr& req) {
    if (!Security::Auth::is_initialized()) {
        return "";
    }
    const auto& auth = Security::Auth::get();
    const auto& cfg = auth.config();
    const std::string token = Security::Auth::extract_access_token(req, cfg.cookies);
    if (token.empty()) {
        return "";
    }
    try {
        std::string err;
        const auto p = auth.verify_jwt(token, err);
        if (!p || p->subject.empty() || Security::Sessions::access_revoked(cfg.cookies, p->subject, p->raw_claims)) {
            return "";
        }
        return p->subject;
    } catch (const std::exception&) {
        return "";
    }
}

void redirect(const CalendarController::Callback& callback, const std::string& outcome) {
    callback(drogon::HttpResponse::newRedirectionResponse("/account?calendar=" + outcome));
}

}  // namespace

void CalendarController::status(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    try {
        const bool on = Calendar::settings().has_value();
        const auto link = LinkRepository().status(owner);
        callback(Response::ok(json{{"data", {{"configured", on}, {"link", link ? *link : json(nullptr)}}}}));
    } catch (const std::exception& e) {
        spdlog::error("calendar.status failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void CalendarController::connect(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    API_REFUSE_API_KEY(req, callback);
    const auto s = Calendar::settings();
    if (!s) {
        callback(ErrorResponse::not_found("calendar_not_configured"));
        return;
    }
    try {
        const std::string state = LinkRepository().new_state(owner);
        callback(Response::ok(json{{"data", {{"url", Calendar::auth_url(*s, state)}}}}));
    } catch (const std::exception& e) {
        spdlog::error("calendar.connect failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void CalendarController::oauthCallback(const HttpRequestPtr& req, Callback&& callback) {
    // Public: Google sends the browser here; the one-time state names the user, who must be the one signed in.
    const auto s = Calendar::settings();
    if (!s) {
        redirect(callback, "error");
        return;
    }
    try {
        LinkRepository links;
        const auto owner = links.take_state(req->getParameter("state"), session_of(req));
        if (!owner) {
            redirect(callback, "expired");
            return;
        }
        if (!req->getParameter("error").empty() || req->getParameter("code").empty()) {
            redirect(callback, "denied");
            return;
        }
        const auto tokens = Calendar::exchange(Net::Http::transport(), *s, req->getParameter("code"));
        if (!tokens.ok() || tokens.refresh_token.empty()) {
            spdlog::warn("calendar: the code exchange failed: {}",
                         tokens.error.empty() ? "no refresh token" : tokens.error);
            redirect(callback, "error");
            return;
        }
        const auto sealed = Utils::Seal::seal(tokens.refresh_token, s->token_key);
        links.set(*owner, tokens.id_email, sealed.ciphertext, sealed.nonce);
        Calendar::forget(*owner);
        Security::Audit::record(*owner, "calendar.link", "calendar_link", *owner);
        redirect(callback, "ok");
    } catch (const std::exception& e) {
        spdlog::error("calendar.callback failed: {}", e.what());
        redirect(callback, "error");
    }
}

void CalendarController::disconnect(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    API_REFUSE_API_KEY(req, callback);
    try {
        LinkRepository links;
        const auto link = links.get(owner);
        if (!link) {
            callback(ErrorResponse::not_found("calendar_not_linked"));
            return;
        }
        if (const auto s = Calendar::settings()) {
            try {
                Calendar::revoke(Net::Http::transport(),
                                 Utils::Seal::unseal({link->sealed, link->nonce}, s->token_key));
            } catch (const Utils::Seal::SealError&) {
                // Deleting is what matters; a token that does not open cannot be revoked anyway.
            }
        }
        links.remove(owner);
        Calendar::forget(owner);
        Security::Audit::record(owner, "calendar.unlink", "calendar_link", owner);
        no_content(callback);
    } catch (const std::exception& e) {
        spdlog::error("calendar.disconnect failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void CalendarController::events(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    const std::string from = req->getParameter("from");
    const std::string to = req->getParameter("to");
    if (!Fields::is_calendar_date(from) || !Fields::is_calendar_date(to) || to < from) {
        callback(ErrorResponse::bad_request("invalid_range", "from and to are days, to not before from"));
        return;
    }
    const std::string tz = tz_of(req);
    try {
        LinkRepository links;
        const long long a = links.local_instant(from, 0, tz);
        const long long b = links.local_instant(to, 24, tz);
        if (b - a > kMaxRangeDays * 86400LL + 7200) {
            callback(ErrorResponse::bad_request("invalid_range", "at most 31 days"));
            return;
        }
        auto out = Calendar::events(Net::Http::transport(), owner, a, b, tz);
        if (auto* p = std::get_if<Calendar::Problem>(&out)) {
            callback(problem(*p));
            return;
        }
        json list = json::array();
        for (const auto& e : std::get<std::vector<Calendar::Event>>(out)) {
            list.push_back(Calendar::event_view(e));
        }
        callback(Response::ok(json{{"data", list}}));
    } catch (const std::exception& e) {
        spdlog::error("calendar.events failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void CalendarController::freeSlots(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    const std::string date = req->getParameter("date");
    if (!Fields::is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_date", "date is a day as YYYY-MM-DD"));
        return;
    }
    int min = 30;
    try {
        if (!req->getParameter("min").empty()) {
            min = std::clamp(std::stoi(req->getParameter("min")), 5, 480);
        }
    } catch (const std::exception&) {}
    const std::string tz = tz_of(req);
    try {
        LinkRepository links;
        // Time already gone today is not free; a past day has none.
        const long long a = std::max(links.local_instant(date, 8, tz), static_cast<long long>(std::time(nullptr)));
        const long long b = links.local_instant(date, 22, tz);
        auto out = Calendar::events(Net::Http::transport(), owner, a, b, tz);
        if (auto* p = std::get_if<Calendar::Problem>(&out)) {
            callback(problem(*p));
            return;
        }
        json list = json::array();
        for (const auto& [x, y] : Calendar::free_slots(std::get<std::vector<Calendar::Event>>(out), a, b, min)) {
            list.push_back({{"start", Calendar::rfc3339(x)}, {"end", Calendar::rfc3339(y)}, {"minutes", (y - x) / 60}});
        }
        callback(Response::ok(json{{"data", list}}));
    } catch (const std::exception& e) {
        spdlog::error("calendar.free failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void CalendarController::add(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    try {
        json body;
        if (!parse_object(req, body, callback)) {
            return;
        }
        const std::string title = body.value("title", std::string());
        if (title.empty() || title.size() > 300) {
            callback(ErrorResponse::bad_request("invalid_title", "title is 1 to 300 characters"));
            return;
        }
        const auto span = span_of(body, callback);
        if (!span) {
            return;
        }
        auto token = Calendar::access(Net::Http::transport(), owner);
        if (auto* p = std::get_if<Calendar::Problem>(&token)) {
            callback(problem(*p));
            return;
        }
        json event{{"summary", title}, {"start", {{"dateTime", span->first}}}, {"end", {{"dateTime", span->second}}}};
        if (body.value("description", json()).is_string()) {
            event["description"] = body["description"].get<std::string>().substr(0, 2000);
        }
        const auto r = Calendar::api(
            Net::Http::transport(), std::get<std::string>(token), "POST", "/calendars/primary/events", {}, event);
        if (!r.ok() || !r.body.is_object()) {
            callback(ErrorResponse::make({drogon::k502BadGateway, "calendar_unavailable", "Google did not take it"}));
            return;
        }
        Security::Audit::record(owner, "calendar.add", "calendar_event", r.body.value("id", std::string()));
        const auto parsed = Calendar::parse_events(json{{"items", json::array({r.body})}}, nullptr);
        callback(Response::created(json{{"data", parsed.empty() ? json::object() : Calendar::event_view(parsed[0])}}));
    } catch (const std::exception& e) {
        spdlog::error("calendar.add failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void CalendarController::one(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_REQUIRE_USER(req, callback, owner);
    if (!event_id_ok(id)) {
        callback(ErrorResponse::not_found("calendar_event_not_found"));
        return;
    }
    const std::string tz = tz_of(req);
    try {
        auto token = Calendar::access(Net::Http::transport(), owner);
        if (auto* p = std::get_if<Calendar::Problem>(&token)) {
            callback(problem(*p));
            return;
        }
        const auto r = Calendar::api(
            Net::Http::transport(), std::get<std::string>(token), "GET", "/calendars/primary/events/" + id);
        const auto parsed = r.ok() && r.body.is_object()
                                ? Calendar::parse_events(json{{"items", json::array({r.body})}}, nullptr)
                                : std::vector<Calendar::Event>{};
        if (r.status == 404 || r.status == 410 || (r.ok() && parsed.empty())) {
            callback(ErrorResponse::not_found("calendar_event_not_found"));
            return;
        }
        if (!r.ok()) {
            callback(ErrorResponse::make({drogon::k502BadGateway, "calendar_unavailable", "Google did not answer"}));
            return;
        }
        const auto& e = parsed[0];
        json view = Calendar::event_view(e);
        LinkRepository links;
        view["start_local"] = e.all_day ? e.start_text : links.local_rfc3339(e.start, tz);
        view["end_local"] = e.all_day ? e.end_text : links.local_rfc3339(e.end, tz);
        callback(Response::ok(json{{"data", view}}));
    } catch (const std::exception& ex) {
        spdlog::error("calendar.event failed: {}", ex.what());
        callback(ErrorResponse::internal_error());
    }
}

void CalendarController::move(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_REQUIRE_USER(req, callback, owner);
    try {
        if (!event_id_ok(id)) {
            callback(ErrorResponse::not_found("calendar_event_not_found"));
            return;
        }
        json body;
        if (!parse_object(req, body, callback)) {
            return;
        }
        const auto span = span_of(body, callback);
        if (!span) {
            return;
        }
        auto token = Calendar::access(Net::Http::transport(), owner);
        if (auto* p = std::get_if<Calendar::Problem>(&token)) {
            callback(problem(*p));
            return;
        }
        json patch{{"start", {{"dateTime", span->first}}}, {"end", {{"dateTime", span->second}}}};
        if (body.value("title", json()).is_string() && !body["title"].get<std::string>().empty()) {
            patch["summary"] = body["title"].get<std::string>().substr(0, 300);
        }
        const auto r = Calendar::api(Net::Http::transport(),
                                     std::get<std::string>(token),
                                     "PATCH",
                                     "/calendars/primary/events/" + id,
                                     {},
                                     patch);
        if (r.status == 404 || r.status == 410) {
            callback(ErrorResponse::not_found("calendar_event_not_found"));
            return;
        }
        if (!r.ok()) {
            callback(ErrorResponse::make({drogon::k502BadGateway, "calendar_unavailable", "Google did not take it"}));
            return;
        }
        Security::Audit::record(owner, "calendar.move", "calendar_event", id);
        callback(Response::ok(json{{"data", {{"moved", true}}}}));
    } catch (const std::exception& e) {
        spdlog::error("calendar.move failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void CalendarController::remove(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_REQUIRE_USER(req, callback, owner);
    try {
        if (!event_id_ok(id)) {
            callback(ErrorResponse::not_found("calendar_event_not_found"));
            return;
        }
        auto token = Calendar::access(Net::Http::transport(), owner);
        if (auto* p = std::get_if<Calendar::Problem>(&token)) {
            callback(problem(*p));
            return;
        }
        const auto r = Calendar::api(
            Net::Http::transport(), std::get<std::string>(token), "DELETE", "/calendars/primary/events/" + id);
        if (r.status == 404 || r.status == 410) {
            callback(ErrorResponse::not_found("calendar_event_not_found"));
            return;
        }
        if (!r.ok()) {
            callback(ErrorResponse::make({drogon::k502BadGateway, "calendar_unavailable", "Google did not take it"}));
            return;
        }
        Security::Audit::record(owner, "calendar.remove", "calendar_event", id);
        no_content(callback);
    } catch (const std::exception& e) {
        spdlog::error("calendar.remove failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

}  // namespace Api
