/**
 * @file CalendarActions.cpp
 * @brief The calendar write tools, calendar_add, calendar_move and calendar_remove
 *        (agent/ActionKit.hpp), with the RFC 3339 helpers they read times with.
 */

#include <ctime>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <variant>

#include "agent/ActionKit.hpp"
#include "agent/SiteApi.hpp"
#include "calendar/Google.hpp"
#include "utils/Crypto.hpp"

namespace Agent::Actions::detail {

namespace {

/// The longest event the calendar route takes (api/CalendarController.cpp, span_of).
constexpr long long kMaxEventSeconds = 24 * 3600;

const json kTime = {{"type", "string"},
                    {"description", "RFC 3339 with the person's offset, like 2026-10-29T14:00:00+05:00"}};
const json kEvent = {{"type", "string"}, {"description", "The event's id from calendar_events"}};
const json kTitle = {{"type", "string"}, {"description", "The event's title"}};

/// An RFC 3339 time with its offset ("2026-10-29T14:00:00+05:00"); empty when it is not one.
std::string time_arg(const json& args, const char* key) {
    const std::string v = text_arg(args, key);
    return ::Calendar::parse_time(v) ? v : std::string();
}

/// "2026-10-29 14:00–15:30" from two times written in the person's offset.
std::string span_words(const std::string& start, const std::string& end) {
    const std::string day = start.substr(0, 10);
    const std::string to = end.substr(0, 10) == day ? end.substr(11, 5) : end.substr(0, 10) + " " + end.substr(11, 5);
    return day + " " + start.substr(11, 5) + "–" + to;
}

std::string event_id(const json& args) {
    static const std::regex kId("^[A-Za-z0-9_-]{1,1024}$");
    const std::string v = text_arg(args, "event");
    return std::regex_match(v, kId) ? v : std::string();
}

/// The UTC offset in seconds an RFC 3339 time is written with ("+05:00" is 18000, "Z" is 0); nullopt when none.
std::optional<long long> offset_of(const std::string& t) {
    const auto at = ::Calendar::parse_time(t);
    if (!at || t.size() < 20) {
        return std::nullopt;
    }
    // The wall clock as written, read as if it were UTC, less the instant it names.
    const auto wall = ::Calendar::parse_time(t.substr(0, 19) + "Z");
    return wall ? std::optional<long long>(*wall - *at) : std::nullopt;
}

/// "2026-10-29 14:00" of the instant @p t on a clock @p offset seconds from UTC.
std::string wall_words(long long t, long long offset) {
    const std::time_t tt = static_cast<std::time_t>(t + offset);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[24];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

/// "2026-10-29 14:00–15:30" of two instants on a clock @p offset seconds from UTC.
std::string span_at(long long a, long long b, long long offset) {
    const std::string from = wall_words(a, offset);
    const std::string to = wall_words(b, offset);
    return from + "–" + (to.substr(0, 10) == from.substr(0, 10) ? to.substr(11) : to);
}

/// The event as the calendar has it ({title, start_local, end_local, ...}), read as the person.
std::variant<json, Proposed> calendar_event(const Ctx& c, const std::string& id) {
    const auto data = SiteApi::read_data(
        c.transport, c.api_url, c.token, "/api/v1/calendar/events/" + id, json::array({json::array({"tz", c.tz})}));
    if (!data || !data->is_object() || (!data->value("all_day", false) && !offset_of(data->value("start_local", "")))) {
        return Proposed{refusal("not_available", "this event is not in the person's calendar")};
    }
    return *data;
}

/// "«Lunch» (2026-10-29 13:00–14:00)": the event's own title and time, and the offset of its clock.
std::pair<std::string, long long> event_words(const json& e) {
    const std::string start = e.value("start_local", "");
    const std::string end = e.value("end_local", start);
    const long long offset = offset_of(start).value_or(0);
    const std::string when = e.value("all_day", false)
                                 ? start.substr(0, 10)
                                 : span_at(*::Calendar::parse_time(start),
                                           ::Calendar::parse_time(end).value_or(*::Calendar::parse_time(start)),
                                           offset);
    return {"«" + cut(e.value("title", std::string("the event")), 120) + "» (" + when + ")", offset};
}

}  // namespace

json calendar_add_tool() {
    return tool("calendar_add",
                "Propose a new event in the person's Google Calendar. Nothing is added until they confirm it.",
                object({{"title", kTitle}, {"start", kTime}, {"end", kTime}}, json::array({"title", "start", "end"})));
}

json calendar_move_tool() {
    return tool("calendar_move",
                "Propose a new time for an event of the person's calendar. Nothing changes until they confirm it.",
                object({{"event", kEvent}, {"title", kTitle}, {"start", kTime}, {"end", kTime}},
                       json::array({"event", "start", "end"})));
}

json calendar_remove_tool() {
    return tool("calendar_remove",
                "Propose to remove an event from the person's calendar. Nothing changes until they confirm it.",
                object({{"event", kEvent}, {"title", kTitle}}, json::array({"event"})));
}

Proposed calendar_add(const Ctx& /*c*/, const json& args) {
    const std::string title = text_arg(args, "title");
    const std::string start = time_arg(args, "start");
    const std::string end = time_arg(args, "end");
    if (title.empty() || length(title) > 300) {
        return refusal("invalid", "the title is 1 to 300 characters");
    }
    if (start.empty() || end.empty() || *::Calendar::parse_time(end) <= *::Calendar::parse_time(start)) {
        return refusal("invalid", "start and end are RFC 3339 times with the person's offset, end after start");
    }
    // The calendar route takes a day at most: a longer one would be confirmed and then refused.
    if (*::Calendar::parse_time(end) - *::Calendar::parse_time(start) > kMaxEventSeconds) {
        return refusal("invalid", "an event lasts 24 hours at most");
    }
    // The event's id is chosen here: a retried add finds the event its first try made (Google refuses
    // a second insert with the id) instead of adding it twice.
    return Proposal{
        "calendar_add",
        "Add «" + cut(title, 120) + "» to the calendar, " + span_words(start, end),
        json{{"method", "POST"},
             {"path", "/api/v1/calendar/events"},
             {"retry", true},
             {"body",
              {{"title", title}, {"start", start}, {"end", end}, {"id", "ani" + Utils::Crypto::random_hex(16)}}}}};
}

Proposed calendar_move(const Ctx& c, const json& args) {
    const std::string id = event_id(args);
    const std::string start = time_arg(args, "start");
    const std::string end = time_arg(args, "end");
    if (id.empty()) {
        return refusal("invalid", "event is an id from calendar_events");
    }
    if (start.empty() || end.empty() || *::Calendar::parse_time(end) <= *::Calendar::parse_time(start)) {
        return refusal("invalid", "start and end are RFC 3339 times with the person's offset, end after start");
    }
    // The calendar route takes a day at most: a longer one would be confirmed and then refused.
    if (*::Calendar::parse_time(end) - *::Calendar::parse_time(start) > kMaxEventSeconds) {
        return refusal("invalid", "an event lasts 24 hours at most");
    }
    auto event = calendar_event(c, id);
    if (auto* p = std::get_if<Proposed>(&event)) {
        return std::move(*p);
    }
    const auto& e = std::get<json>(event);
    if (e.value("all_day", false)) {
        return refusal("invalid", "an all-day event is moved in Google Calendar itself");
    }
    // The new time on the event's own clock, whatever offset Ani wrote it with.
    const auto [what, offset] = event_words(e);
    return Proposal{
        "calendar_move",
        "Move " + what + " to " + span_at(*::Calendar::parse_time(start), *::Calendar::parse_time(end), offset),
        json{{"method", "PATCH"},
             {"path", "/api/v1/calendar/events/" + id},
             {"body", {{"start", start}, {"end", end}}}}};
}

Proposed calendar_remove(const Ctx& c, const json& args) {
    const std::string id = event_id(args);
    if (id.empty()) {
        return refusal("invalid", "event is an id from calendar_events");
    }
    auto event = calendar_event(c, id);
    if (auto* p = std::get_if<Proposed>(&event)) {
        return std::move(*p);
    }
    return Proposal{"calendar_remove",
                    "Remove " + event_words(std::get<json>(event)).first + " from the calendar",
                    json{{"method", "DELETE"}, {"path", "/api/v1/calendar/events/" + id}}};
}

}  // namespace Agent::Actions::detail
