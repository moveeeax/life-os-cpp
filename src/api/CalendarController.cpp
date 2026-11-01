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
#include "api/Outbound.hpp"
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
#include "utils/Utf8.hpp"

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
    std::optional<std::string> owner;
    try {
        owner = LinkRepository().take_state(req->getParameter("state"), session_of(req));
    } catch (const std::exception& e) {
        spdlog::error("calendar.callback failed: {}", e.what());
        redirect(callback, "error");
        return;
    }
    if (!owner) {
        redirect(callback, "expired");
        return;
    }
    if (!req->getParameter("error").empty() || req->getParameter("code").empty()) {
        redirect(callback, "denied");
        return;
    }
    // The code exchange waits on Google: off the IO thread.
    Outbound::run(std::move(callback),
                  "calendar.callback",
                  [settings = *s, who = *owner, code = req->getParameter("code")](const Callback& reply) {
                      try {
                          const auto tokens = Calendar::exchange(Net::Http::transport(), settings, code);
                          if (!tokens.ok() || tokens.refresh_token.empty()) {
                              spdlog::warn("calendar: the code exchange failed: {}",
                                           tokens.error.empty() ? "no refresh token" : tokens.error);
                              redirect(reply, "error");
                              return;
                          }
                          const auto sealed = Utils::Seal::seal(tokens.refresh_token, settings.token_key);
                          LinkRepository().set(who, tokens.id_email, sealed.ciphertext, sealed.nonce);
                          Calendar::forget(who);
                          CalendarCache::forget(who);
                          Security::Audit::record(who, "calendar.link", "calendar_link", who);
                          redirect(reply, "ok");
                      } catch (const std::exception& e) {
                          spdlog::error("calendar.callback failed: {}", e.what());
                          redirect(reply, "error");
                      }
                  });
}

void CalendarController::disconnect(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    API_REFUSE_API_KEY(req, callback);
    // The revoke waits on Google: off the IO thread.
    Outbound::run(std::move(callback), "calendar.disconnect", [owner](const Callback& reply) {
        LinkRepository links;
        const auto link = links.get(owner);
        if (!link) {
            reply(ErrorResponse::not_found("calendar_not_linked"));
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
        CalendarCache::forget(owner);
        Security::Audit::record(owner, "calendar.unlink", "calendar_link", owner);
        no_content(reply);
    });
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
    Outbound::run(std::move(callback), "calendar.events", [owner, from, to, tz](const Callback& reply) {
        LinkRepository links;
        const long long a = links.local_instant(from, 0, tz);
        const long long b = links.local_instant(to, 24, tz);
        if (b - a > kMaxRangeDays * 86400LL + 7200) {
            reply(ErrorResponse::bad_request("invalid_range", "at most 31 days"));
            return;
        }
        auto out = Calendar::events(Net::Http::transport(), owner, a, b, tz);
        if (auto* p = std::get_if<Calendar::Problem>(&out)) {
            reply(problem(*p));
            return;
        }
        json list = json::array();
        for (const auto& e : std::get<std::vector<Calendar::Event>>(out)) {
            list.push_back(Calendar::event_view(e));
        }
        reply(Response::ok(json{{"data", list}}));
    });
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
    Outbound::run(std::move(callback), "calendar.free", [owner, date, tz, min](const Callback& reply) {
        LinkRepository links;
        // Time already gone today is not free; a past day has none.
        const long long a = std::max(links.local_instant(date, 8, tz), static_cast<long long>(std::time(nullptr)));
        const long long b = links.local_instant(date, 22, tz);
        if (a >= b) {
            // A past day, or today after 22:00: no free time, and nothing to ask Google (an empty range is a 400).
            reply(Response::ok(json{{"data", json::array()}}));
            return;
        }
        auto out = Calendar::events(Net::Http::transport(), owner, a, b, tz);
        if (auto* p = std::get_if<Calendar::Problem>(&out)) {
            reply(problem(*p));
            return;
        }
        json list = json::array();
        for (const auto& [x, y] : Calendar::free_slots(std::get<std::vector<Calendar::Event>>(out), a, b, min)) {
            list.push_back({{"start", Calendar::rfc3339(x)}, {"end", Calendar::rfc3339(y)}, {"minutes", (y - x) / 60}});
        }
        reply(Response::ok(json{{"data", list}}));
    });
}

void CalendarController::add(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const std::string title = Fields::text_or_empty(body, "title").value_or("");
    if (title.empty() || Utils::Utf8::length(title) > 300) {
        callback(ErrorResponse::bad_request("invalid_title", "title is 1 to 300 characters"));
        return;
    }
    const auto span = span_of(body, callback);
    if (!span) {
        return;
    }
    // A caller's own event id (Ani's confirmed add): a retry after a timeout finds the event the
    // first try made instead of adding it twice.
    std::string id;
    if (body.contains("id")) {
        id = body["id"].is_string() ? body["id"].get<std::string>() : std::string();
        if (!Calendar::client_event_id_ok(id)) {
            callback(ErrorResponse::bad_request("invalid_id", "id is 5 to 1024 of a-v and 0-9"));
            return;
        }
    }
    json event{{"summary", title}, {"start", {{"dateTime", span->first}}}, {"end", {{"dateTime", span->second}}}};
    if (body.value("description", json()).is_string()) {
        event["description"] = Utils::Utf8::cut(body["description"].get<std::string>(), 2000);
    }
    if (!id.empty()) {
        event["id"] = id;
    }
    Outbound::run(std::move(callback), "calendar.add", [owner, event, id](const Callback& reply) {
        auto token = Calendar::access(Net::Http::transport(), owner);
        if (auto* p = std::get_if<Calendar::Problem>(&token)) {
            reply(problem(*p));
            return;
        }
        const std::string& bearer = std::get<std::string>(token);
        auto r = Calendar::api(Net::Http::transport(), bearer, "POST", "/calendars/primary/events", {}, event);
        if (r.status == 409 && !id.empty()) {
            // The id is taken: an earlier try of this same add went through. Its event is the answer.
            r = Calendar::api(Net::Http::transport(), bearer, "GET", "/calendars/primary/events/" + id);
        }
        if (r.status == 401) {
            Calendar::forget(owner);
        }
        if (!r.ok() || !r.body.is_object()) {
            reply(ErrorResponse::make({drogon::k502BadGateway, "calendar_unavailable", "Google did not take it"}));
            return;
        }
        CalendarCache::forget(owner);
        Security::Audit::record(owner, "calendar.add", "calendar_event", r.body.value("id", std::string()));
        const auto parsed = Calendar::parse_events(json{{"items", json::array({r.body})}}, nullptr);
        if (parsed.empty()) {
            // The 201 promises a whole event; an empty one is not that. Google
            // may hold the event already, so the message says to look first.
            reply(ErrorResponse::make(
                {drogon::k502BadGateway,
                 "calendar_unavailable",
                 "Google's answer could not be read; the event may be in the calendar, check before adding it again"}));
            return;
        }
        reply(Response::created(json{{"data", Calendar::event_view(parsed[0])}}));
    });
}

void CalendarController::one(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_REQUIRE_USER(req, callback, owner);
    if (!event_id_ok(id)) {
        callback(ErrorResponse::not_found("calendar_event_not_found"));
        return;
    }
    const std::string tz = tz_of(req);
    Outbound::run(std::move(callback), "calendar.event", [owner, id, tz](const Callback& reply) {
        auto token = Calendar::access(Net::Http::transport(), owner);
        if (auto* p = std::get_if<Calendar::Problem>(&token)) {
            reply(problem(*p));
            return;
        }
        const auto r = Calendar::api(
            Net::Http::transport(), std::get<std::string>(token), "GET", "/calendars/primary/events/" + id);
        if (r.status == 401) {
            Calendar::forget(owner);
        }
        const auto parsed = r.ok() && r.body.is_object()
                                ? Calendar::parse_events(json{{"items", json::array({r.body})}}, nullptr)
                                : std::vector<Calendar::Event>{};
        if (r.status == 404 || r.status == 410 || (r.ok() && parsed.empty())) {
            reply(ErrorResponse::not_found("calendar_event_not_found"));
            return;
        }
        if (!r.ok()) {
            reply(ErrorResponse::make({drogon::k502BadGateway, "calendar_unavailable", "Google did not answer"}));
            return;
        }
        const auto& e = parsed[0];
        json view = Calendar::event_view(e);
        LinkRepository links;
        view["start_local"] = e.all_day ? e.start_text : links.local_rfc3339(e.start, tz);
        view["end_local"] = e.all_day ? e.end_text : links.local_rfc3339(e.end, tz);
        reply(Response::ok(json{{"data", view}}));
    });
}

void CalendarController::move(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_REQUIRE_USER(req, callback, owner);
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
    json patch{{"start", {{"dateTime", span->first}}}, {"end", {{"dateTime", span->second}}}};
    if (body.value("title", json()).is_string() && !body["title"].get<std::string>().empty()) {
        patch["summary"] = Utils::Utf8::cut(body["title"].get<std::string>(), 300);
    }
    Outbound::run(std::move(callback), "calendar.move", [owner, id, patch](const Callback& reply) {
        auto token = Calendar::access(Net::Http::transport(), owner);
        if (auto* p = std::get_if<Calendar::Problem>(&token)) {
            reply(problem(*p));
            return;
        }
        const auto r = Calendar::api(Net::Http::transport(),
                                     std::get<std::string>(token),
                                     "PATCH",
                                     "/calendars/primary/events/" + id,
                                     {},
                                     patch);
        if (r.status == 401) {
            Calendar::forget(owner);
        }
        if (r.status == 404 || r.status == 410) {
            reply(ErrorResponse::not_found("calendar_event_not_found"));
            return;
        }
        if (!r.ok()) {
            reply(ErrorResponse::make({drogon::k502BadGateway, "calendar_unavailable", "Google did not take it"}));
            return;
        }
        CalendarCache::forget(owner);
        Security::Audit::record(owner, "calendar.move", "calendar_event", id);
        reply(Response::ok(json{{"data", {{"moved", true}}}}));
    });
}

void CalendarController::remove(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_REQUIRE_USER(req, callback, owner);
    if (!event_id_ok(id)) {
        callback(ErrorResponse::not_found("calendar_event_not_found"));
        return;
    }
    Outbound::run(std::move(callback), "calendar.remove", [owner, id](const Callback& reply) {
        auto token = Calendar::access(Net::Http::transport(), owner);
        if (auto* p = std::get_if<Calendar::Problem>(&token)) {
            reply(problem(*p));
            return;
        }
        const auto r = Calendar::api(
            Net::Http::transport(), std::get<std::string>(token), "DELETE", "/calendars/primary/events/" + id);
        if (r.status == 401) {
            Calendar::forget(owner);
        }
        if (r.status == 404 || r.status == 410) {
            reply(ErrorResponse::not_found("calendar_event_not_found"));
            return;
        }
        if (!r.ok()) {
            reply(ErrorResponse::make({drogon::k502BadGateway, "calendar_unavailable", "Google did not take it"}));
            return;
        }
        CalendarCache::forget(owner);
        Security::Audit::record(owner, "calendar.remove", "calendar_event", id);
        no_content(reply);
    });
}

}  // namespace Api
