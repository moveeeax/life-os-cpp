/**
 * @file Actions.cpp
 * @brief Bodies for src/agent/Actions.hpp.
 */

#include "agent/Actions.hpp"

#include <algorithm>
#include <ctime>
#include <exception>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <spdlog/spdlog.h>

#include "agent/Settings.hpp"
#include "agent/Tools.hpp"
#include "calendar/Google.hpp"
#include "jobs/Job.hpp"
#include "repositories/agent/ActionRepository.hpp"
#include "storage/S3Presign.hpp"
#include "utils/Crypto.hpp"

namespace Agent::Actions {

namespace {

using json = nlohmann::json;
using Repositories::Agent::ActionRepository;

const json kAreas = {"finance", "health", "travel", "growth", "relationships", "projects"};

json object(json properties, json required) {
    return json{{"type", "object"},
                {"properties", std::move(properties)},
                {"required", std::move(required)},
                {"additionalProperties", false}};
}

json tool(const char* name, const char* description, json parameters) {
    return json{{"type", "function"},
                {"function", {{"name", name}, {"description", description}, {"parameters", std::move(parameters)}}}};
}

std::string text_arg(const json& args, const char* key) {
    const auto it = args.find(key);
    if (it == args.end() || !it->is_string()) {
        return {};
    }
    std::string v = it->get<std::string>();
    const auto first = v.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    return v.substr(first, v.find_last_not_of(" \t\r\n") - first + 1);
}

/// A day argument; anything that is not YYYY-MM-DD is dropped.
std::string day_arg(const json& args, const char* key) {
    static const std::regex kDayRe("^\\d{4}-\\d{2}-\\d{2}$");
    const std::string v = text_arg(args, key);
    return std::regex_match(v, kDayRe) ? v : std::string();
}

std::size_t length(const std::string& s) {
    std::size_t n = 0;
    for (const unsigned char c : s) {
        n += (c & 0xC0) != 0x80 ? 1 : 0;
    }
    return n;
}

/// The first @p max characters of @p s, with an ellipsis when it was longer.
std::string cut(const std::string& s, std::size_t max) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80 && n++ == max) {
            return s.substr(0, i) + "…";
        }
    }
    return s;
}

std::string refusal(const char* error, const std::string& message, json extra = json::object()) {
    extra["error"] = error;
    extra["message"] = message;
    return extra.dump();
}

std::string url_of(const std::string& api_url, const std::string& path, const json& query) {
    std::string url = api_url + path;
    char sep = '?';
    for (const auto& kv : query) {
        url += sep + kv[0].get<std::string>() + "=" + Storage::S3::uri_encode(kv[1].get<std::string>(), true);
        sep = '&';
    }
    return url;
}

Net::Http::Request request_of(const std::string& api_url, const std::string& token, const json& req) {
    Net::Http::Request r;
    r.method = req.value("method", "GET");
    r.url = url_of(api_url, req.value("path", ""), req.value("query", json::array()));
    r.headers = {{"Authorization", "Bearer " + token}, {"Accept", "application/json"}};
    if (req.contains("body")) {
        r.body = req["body"].dump();
        r.content_type = "application/json";
    }
    r.timeout_seconds = 20;
    return r;
}

/// The data of a GET as the user; nullopt when the API refused it.
std::optional<json> read(Net::Http::Transport& transport,
                         const std::string& api_url,
                         const std::string& token,
                         const std::string& path,
                         json query) {
    const auto res = transport.send(request_of(api_url, token, json{{"path", path}, {"query", std::move(query)}}));
    const auto body = json::parse(res.body, nullptr, /*allow_exceptions=*/false);
    if (res.status != 200 || !body.is_object() || !body.contains("data")) {
        return std::nullopt;
    }
    return std::optional<json>(std::in_place, body["data"]);
}

Proposed create_task(const json& args, const std::string& tz) {
    const std::string title = text_arg(args, "title");
    if (title.empty() || length(title) > 200) {
        return refusal("invalid", "the title is 1 to 200 characters");
    }
    const std::string area = text_arg(args, "area");
    if (std::find(kAreas.begin(), kAreas.end(), json(area)) == kAreas.end()) {
        return refusal("invalid", "the area is one of the list", json{{"areas", kAreas}});
    }
    json body = {{"title", title}, {"area", area}};
    std::string summary = "Add the task «" + title + "»";
    if (const std::string due = day_arg(args, "due"); !due.empty()) {
        body["due"] = due;
        summary += ", due " + due;
    }
    return Proposal{"create_task",
                    summary,
                    json{{"method", "POST"},
                         {"path", "/api/v1/tasks/items"},
                         {"query", json::array({json::array({"tz", tz})})},
                         {"body", body}}};
}

Proposed complete_task(Net::Http::Transport& transport,
                       const std::string& api_url,
                       const std::string& token,
                       const json& args,
                       const std::string& tz) {
    const std::string words = text_arg(args, "task");
    if (words.empty()) {
        return refusal("invalid", "say which task: words of its title");
    }
    const auto found =
        read(transport,
             api_url,
             token,
             "/api/v1/tasks/items",
             json::array({json::array({"q", cut(words, 200)}), json::array({"limit", "10"}), json::array({"tz", tz})}));
    if (!found || !found->is_array()) {
        return refusal("unavailable", "the tasks could not be read");
    }
    std::vector<json> open;
    for (const auto& t : *found) {
        if (t.is_object() && t.value("status", "") != "done") {
            open.push_back(t);
        }
    }
    const json* pick = open.size() == 1 ? &open[0] : nullptr;
    for (const auto& t : open) {
        if (pick == nullptr && t.value("title", "") == words) {
            pick = &t;
        }
    }
    if (pick == nullptr) {
        if (open.empty()) {
            return refusal("not_found", "no open task has these words; look it up with find_tasks");
        }
        json titles = json::array();
        for (const auto& t : open) {
            titles.push_back(t.value("title", ""));
        }
        return refusal(
            "ambiguous", "several open tasks match: ask which, or give its full title", json{{"matches", titles}});
    }
    return Proposal{"complete_task",
                    "Close the task «" + pick->value("title", "") + "»",
                    json{{"method", "PATCH"},
                         {"path", "/api/v1/tasks/items/" + pick->value("id", "")},
                         {"query", json::array({json::array({"tz", tz})})},
                         {"body", {{"status", "done"}}}}};
}

Proposed check_habit(Net::Http::Transport& transport,
                     const std::string& api_url,
                     const std::string& token,
                     const json& args,
                     const std::string& tz,
                     const std::string& today) {
    const std::string name = text_arg(args, "habit");
    if (name.empty()) {
        return refusal("invalid", "say which habit: its name as the habits tool gives it");
    }
    const auto habits = read(transport, api_url, token, "/api/v1/habits", json::array());
    if (!habits || !habits->is_array()) {
        return refusal("unavailable", "the habits could not be read");
    }
    const json* pick = nullptr;
    std::vector<const json*> near;
    json names = json::array();
    for (const auto& h : *habits) {
        const std::string n = h.value("name", "");
        names.push_back(n);
        if (n == name) {
            pick = &h;
        } else if (n.find(name) != std::string::npos || name.find(n) != std::string::npos) {
            near.push_back(&h);
        }
    }
    if (pick == nullptr && near.size() == 1) {
        pick = near[0];
    }
    if (pick == nullptr) {
        return refusal(near.empty() ? "not_found" : "ambiguous",
                       "give the habit's name exactly as one of these",
                       json{{"habits", names}});
    }
    const std::string source = pick->value("source", "manual");
    if (source != "manual") {
        return refusal("automatic", "this habit is ticked by its source (" + source + "), not by hand");
    }
    const std::string date = day_arg(args, "date").empty() ? today : day_arg(args, "date");
    if (date.empty() || date > today) {
        return refusal("invalid", "a habit is ticked for today or a past day");
    }
    const std::string habit = pick->value("name", "");
    const std::string path = "/api/v1/habits/" + pick->value("id", "") + "/checks/" + date;
    const std::string day = date == today ? "today" : date;
    const json query = json::array({json::array({"tz", tz})});
    const auto amount = args.find("amount");
    if (amount != args.end() && amount->is_number() && amount->get<double>() > 0 &&
        amount->get<double>() <= 1'000'000) {
        const double value = amount->get<double>();
        json shown = value == static_cast<double>(static_cast<long long>(value)) ? json(static_cast<long long>(value))
                                                                                 : json(value);
        const std::string unit = pick->value("unit", "");
        return Proposal{
            "check_habit",
            "Add " + shown.dump() + (unit.empty() ? "" : " " + unit) + " to «" + habit + "» for " + day,
            json{{"method", "POST"}, {"path", path + "/add"}, {"query", query}, {"body", {{"value", value}}}}};
    }
    return Proposal{"check_habit",
                    "Tick «" + habit + "» for " + day,
                    json{{"method", "PUT"}, {"path", path}, {"query", query}, {"body", json::object()}}};
}

Proposed add_note(const json& args) {
    const std::string text = text_arg(args, "text");
    if (text.empty() || length(text) > 2000) {
        return refusal("invalid", "the note is 1 to 2000 characters");
    }
    return Proposal{"add_note",
                    "Note to the inbox: «" + cut(text, 200) + "»",
                    json{{"method", "POST"}, {"path", "/api/v1/tasks/notes"}, {"body", {{"text", text}}}}};
}

/// The evening diary she writes from a talk: a note under the topic journal for @p today.
Proposed journal_note(const json& args, const std::string& today) {
    const std::string title = text_arg(args, "title");
    const std::string body = text_arg(args, "body");
    if (title.empty() || length(title) > 120) {
        return refusal("invalid", "the title is 1 to 120 characters");
    }
    if (body.empty() || length(body) > 4000) {
        return refusal("invalid", "the entry is 1 to 4000 characters");
    }
    json note{{"title", title}, {"body", body}, {"topic", "journal"}};
    if (today.size() == 10) {
        note["date"] = today;
    }
    return Proposal{"journal_note",
                    "Diary entry «" + cut(title, 80) + "»: «" + cut(body, 200) + "»",
                    json{{"method", "POST"}, {"path", "/api/v1/notes"}, {"body", note}}};
}

/// A Telegram chat or message id argument (digits, a minus for a group); empty when it is not one.
std::string tg_id(const json& args, const char* key) {
    static const std::regex kIdRe("^-?\\d{1,20}$");
    std::string v = text_arg(args, key);
    if (v.empty()) {
        if (const auto it = args.find(key); it != args.end() && it->is_number_integer()) {
            v = std::to_string(it->get<long long>());
        }
    }
    return std::regex_match(v, kIdRe) ? v : std::string();
}

/// The title of the person's chat, or a refusal when Telegram is not theirs or the chat is unknown.
std::variant<std::string, Proposed> chat_title(Net::Http::Transport& transport,
                                               const std::string& api_url,
                                               const std::string& token,
                                               const std::string& chat) {
    const auto data = read(transport, api_url, token, "/api/v1/telegram/chats/" + chat, json::array());
    if (!data || !data->is_object()) {
        return Proposed{refusal("not_available", "this chat is not known or Telegram is off for the person")};
    }
    const std::string title = data->value("title", std::string());
    return title.empty() ? std::string("a chat") : title;
}

Proposed tg_send(Net::Http::Transport& transport,
                 const std::string& api_url,
                 const std::string& token,
                 const json& args) {
    const std::string chat = tg_id(args, "chat");
    const std::string text = text_arg(args, "text");
    if (chat.empty()) {
        return refusal("invalid", "chat is the id from tg_chats");
    }
    if (text.empty() || length(text) > 4000) {
        return refusal("invalid", "the message is 1 to 4000 characters");
    }
    auto title = chat_title(transport, api_url, token, chat);
    if (auto* p = std::get_if<Proposed>(&title)) {
        return std::move(*p);
    }
    json body{{"text", text}, {"key", "ani-" + Utils::Crypto::random_hex(12)}};
    const std::string reply_to = tg_id(args, "reply_to");
    if (!reply_to.empty()) {
        body["reply_to"] = reply_to;
    }
    // The label is the summary before the text: a new text from the person keeps it (retext).
    const std::string label = "Send to " + cut(std::get<std::string>(title), 60) + ": ";
    return Proposal{"tg_send",
                    label + "«" + cut(text, 300) + "»",
                    json{{"method", "POST"},
                         {"path", "/api/v1/telegram/chats/" + chat + "/messages"},
                         {"body", body},
                         {"label", label}}};
}

Proposed tg_edit(Net::Http::Transport& transport,
                 const std::string& api_url,
                 const std::string& token,
                 const json& args) {
    const std::string chat = tg_id(args, "chat");
    const std::string message = tg_id(args, "message");
    const std::string text = text_arg(args, "text");
    if (chat.empty() || message.empty()) {
        return refusal("invalid", "chat and message are ids from tg_chats and tg_history");
    }
    if (text.empty() || length(text) > 4000) {
        return refusal("invalid", "the message is 1 to 4000 characters");
    }
    auto title = chat_title(transport, api_url, token, chat);
    if (auto* p = std::get_if<Proposed>(&title)) {
        return std::move(*p);
    }
    const std::string label = "Edit your message in " + cut(std::get<std::string>(title), 60) + " to ";
    return Proposal{"tg_edit",
                    label + "«" + cut(text, 300) + "»",
                    json{{"method", "PATCH"},
                         {"path", "/api/v1/telegram/chats/" + chat + "/messages/" + message},
                         {"body", {{"text", text}}},
                         {"label", label}}};
}

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

Proposed calendar_add(const json& args) {
    const std::string title = text_arg(args, "title");
    const std::string start = time_arg(args, "start");
    const std::string end = time_arg(args, "end");
    if (title.empty() || length(title) > 300) {
        return refusal("invalid", "the title is 1 to 300 characters");
    }
    if (start.empty() || end.empty() || *::Calendar::parse_time(end) <= *::Calendar::parse_time(start)) {
        return refusal("invalid", "start and end are RFC 3339 times with the person's offset, end after start");
    }
    return Proposal{"calendar_add",
                    "Add «" + cut(title, 120) + "» to the calendar, " + span_words(start, end),
                    json{{"method", "POST"},
                         {"path", "/api/v1/calendar/events"},
                         {"body", {{"title", title}, {"start", start}, {"end", end}}}}};
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
std::variant<json, Proposed> calendar_event(Net::Http::Transport& transport,
                                            const std::string& api_url,
                                            const std::string& token,
                                            const std::string& tz,
                                            const std::string& id) {
    const auto data =
        read(transport, api_url, token, "/api/v1/calendar/events/" + id, json::array({json::array({"tz", tz})}));
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

Proposed calendar_move(Net::Http::Transport& transport,
                       const std::string& api_url,
                       const std::string& token,
                       const std::string& tz,
                       const json& args) {
    const std::string id = event_id(args);
    const std::string start = time_arg(args, "start");
    const std::string end = time_arg(args, "end");
    if (id.empty()) {
        return refusal("invalid", "event is an id from calendar_events");
    }
    if (start.empty() || end.empty() || *::Calendar::parse_time(end) <= *::Calendar::parse_time(start)) {
        return refusal("invalid", "start and end are RFC 3339 times with the person's offset, end after start");
    }
    auto event = calendar_event(transport, api_url, token, tz, id);
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

Proposed calendar_remove(Net::Http::Transport& transport,
                         const std::string& api_url,
                         const std::string& token,
                         const std::string& tz,
                         const json& args) {
    const std::string id = event_id(args);
    if (id.empty()) {
        return refusal("invalid", "event is an id from calendar_events");
    }
    auto event = calendar_event(transport, api_url, token, tz, id);
    if (auto* p = std::get_if<Proposed>(&event)) {
        return std::move(*p);
    }
    return Proposal{"calendar_remove",
                    "Remove " + event_words(std::get<json>(event)).first + " from the calendar",
                    json{{"method", "DELETE"}, {"path", "/api/v1/calendar/events/" + id}}};
}

}  // namespace

json definitions() {
    const json day = {{"type", "string"}, {"description", "A day as YYYY-MM-DD"}};
    return json::array({
        tool(
            "create_task",
            "Propose a new task. Nothing is written until the person confirms it with a button.",
            object({{"title", {{"type", "string"}, {"description", "The task, short, in the person's words"}}},
                    {"area", {{"type", "string"}, {"enum", kAreas}, {"description", "The part of life it belongs to"}}},
                    {"due", day}},
                   json::array({"title", "area"}))),
        tool("complete_task",
             "Propose to close an open task as done. Nothing changes until the person confirms it.",
             object({{"task", {{"type", "string"}, {"description", "Its title, or words of it"}}}},
                    json::array({"task"}))),
        tool("check_habit",
             "Propose to tick a habit for a day (today unless given), or add an amount to it. Nothing changes "
             "until the person confirms it.",
             object({{"habit", {{"type", "string"}, {"description", "Its name as the habits tool gives it"}}},
                     {"date", day},
                     {"amount", {{"type", "number"}, {"description", "An amount to add, in the habit's unit"}}}},
                    json::array({"habit"}))),
        tool("add_note",
             "Propose a note to the person's inbox, to sort later. Nothing is written until they confirm it.",
             object({{"text", {{"type", "string"}, {"description", "The note"}}}}, json::array({"text"}))),
        tool("journal_note",
             "Propose today's diary entry, written from what the person told you about their day, in their words "
             "and first person. Nothing is written until they confirm it.",
             object({{"title", {{"type", "string"}, {"description", "A few words for the day"}}},
                     {"body", {{"type", "string"}, {"description", "The entry, a few short paragraphs"}}}},
                    json::array({"title", "body"}))),
    });
}

json telegram_definitions() {
    const json id = {{"type", "string"}, {"description", "An id as tg_chats and tg_history give it"}};
    return json::array({
        tool("tg_send",
             "Propose a Telegram message from the person to a chat, as a reply to a message if given. Nothing is "
             "sent until they confirm it; they may change the text first.",
             object({{"chat", id},
                     {"text", {{"type", "string"}, {"description", "The message, in the person's voice"}}},
                     {"reply_to", id}},
                    json::array({"chat", "text"}))),
        tool("tg_edit",
             "Propose a new text for one of the person's own Telegram messages. Nothing changes until they "
             "confirm it.",
             object({{"chat", id}, {"message", id}, {"text", {{"type", "string"}, {"description", "The new text"}}}},
                    json::array({"chat", "message", "text"}))),
    });
}

json calendar_definitions() {
    const json time = {{"type", "string"},
                       {"description", "RFC 3339 with the person's offset, like 2026-10-29T14:00:00+05:00"}};
    const json event = {{"type", "string"}, {"description", "The event's id from calendar_events"}};
    const json title = {{"type", "string"}, {"description", "The event's title"}};
    return json::array({
        tool("calendar_add",
             "Propose a new event in the person's Google Calendar. Nothing is added until they confirm it.",
             object({{"title", title}, {"start", time}, {"end", time}}, json::array({"title", "start", "end"}))),
        tool("calendar_move",
             "Propose a new time for an event of the person's calendar. Nothing changes until they confirm it.",
             object({{"event", event}, {"title", title}, {"start", time}, {"end", time}},
                    json::array({"event", "start", "end"}))),
        tool("calendar_remove",
             "Propose to remove an event from the person's calendar. Nothing changes until they confirm it.",
             object({{"event", event}, {"title", title}}, json::array({"event"}))),
    });
}

bool is_write(const std::string& name) {
    return name == "create_task" || name == "complete_task" || name == "check_habit" || name == "add_note" ||
           name == "journal_note" || name == "tg_send" || name == "tg_edit" || name == "calendar_add" ||
           name == "calendar_move" || name == "calendar_remove";
}

Proposed propose(Net::Http::Transport& transport,
                 const std::string& api_url,
                 const std::string& access_token,
                 const std::string& tz,
                 const std::string& today,
                 const std::string& name,
                 const json& raw) {
    const json args = raw.is_object() ? raw : json::object();
    try {
        if (name == "create_task") {
            return create_task(args, tz);
        }
        if (name == "complete_task") {
            return complete_task(transport, api_url, access_token, args, tz);
        }
        if (name == "check_habit") {
            return check_habit(transport, api_url, access_token, args, tz, today);
        }
        if (name == "add_note") {
            return add_note(args);
        }
        if (name == "journal_note") {
            return journal_note(args, today);
        }
        if (name == "calendar_add") {
            return calendar_add(args);
        }
        if (name == "calendar_move") {
            return calendar_move(transport, api_url, access_token, tz, args);
        }
        if (name == "calendar_remove") {
            return calendar_remove(transport, api_url, access_token, tz, args);
        }
        if (name == "tg_send") {
            return tg_send(transport, api_url, access_token, args);
        }
        if (name == "tg_edit") {
            return tg_edit(transport, api_url, access_token, args);
        }
    } catch (const Net::Http::TransportError& e) {
        spdlog::warn("ani agent: the tool {} got no answer: {}", name, e.what());
        return refusal("unavailable", "Life OS did not answer");
    }
    return refusal("unknown_tool", "there is no tool " + name);
}

std::string proposed_text(const Proposal& p) {
    return json{{"proposed", p.summary},
                {"message",
                 "Nothing is done yet: the person sees this under your reply with Confirm and Cancel buttons. "
                 "Say in a few words what you propose and ask them to confirm it."}}
        .dump();
}

std::string execute(Net::Http::Transport& transport,
                    const std::string& api_url,
                    const std::string& access_token,
                    const json& request) {
    Net::Http::Response res;
    try {
        res = transport.send(request_of(api_url, access_token, request));
    } catch (const Net::Http::TransportError& e) {
        spdlog::warn("ani agent: an action got no answer: {}", e.what());
        return "unavailable";
    }
    if (res.status >= 200 && res.status < 300) {
        return {};
    }
    const auto body = json::parse(res.body, nullptr, /*allow_exceptions=*/false);
    if (body.is_object() && body.value("error", json()).is_string()) {
        return cut(body["error"].get<std::string>(), 60);
    }
    return "http_" + std::to_string(res.status);
}

Gateway::ToolRunner runner(Net::Http::Transport& transport,
                           const std::string& api_url,
                           const std::string& access_token,
                           const std::string& tz,
                           const std::string& today,
                           const std::string& owner,
                           const std::string& turn_id,
                           Gateway::ToolRunner reads) {
    return [&transport, api_url, access_token, tz, today, owner, turn_id, reads = std::move(reads)](
               const std::string& name, const json& args) -> std::string {
        if (!is_write(name)) {
            return reads(name, args);
        }
        try {
            ActionRepository actions;
            if (actions.count_for_turn(turn_id) >= kMaxPerTurn) {
                return refusal("too_many", "at most " + std::to_string(kMaxPerTurn) + " changes per message");
            }
            auto out = propose(transport, api_url, access_token, tz, today, name, args);
            if (auto* text = std::get_if<std::string>(&out)) {
                return *text;
            }
            const auto& p = std::get<Proposal>(out);
            actions.add(owner, turn_id, p.kind, p.summary, p.request);
            return proposed_text(p);
        } catch (const std::exception& e) {
            spdlog::warn("ani agent: the proposal {} failed: {}", name, e.what());
            return refusal("failed", "the change could not be proposed");
        }
    };
}

json process_action(const json& payload) {
    const std::string id = payload.value("action", std::string());
    if (id.empty()) {
        throw Jobs::PermanentJobError("ani action job without an action");
    }
    ActionRepository actions;
    const auto a = actions.for_worker(id);
    if (!a) {
        return json{{"status", "skipped"}, {"reason", "no_action"}};
    }
    const std::string status = a->value("status", "");
    if (status == "running") {
        // A worker died inside this action: it may have gone through, so it is not run again.
        actions.finish(id, "interrupted");
        return json{{"status", "failed"}, {"error", "interrupted"}};
    }
    if (status != "confirmed") {
        return json{{"status", "skipped"}, {"reason", status}};
    }
    const auto s = read_settings();
    const auto token = s && !s->api_url.empty() ? Tools::access_token(a->value("owner_id", "")) : std::nullopt;
    if (!token) {
        actions.finish(id, "unavailable");
        return json{{"status", "failed"}, {"error", "unavailable"}};
    }
    if (!actions.start(id)) {
        return json{{"status", "skipped"}, {"reason", "not_confirmed"}};
    }
    const std::string error = execute(Net::Http::transport(), s->api_url, *token, (*a)["request"]);
    actions.finish(id, error);
    if (!error.empty()) {
        spdlog::warn("ani agent: action {} failed: {}", id, error);
        return json{{"status", "failed"}, {"error", error}};
    }
    return json{{"status", "done"}};
}

void fail_action(const std::string& action_id, const std::string& error) {
    try {
        if (!action_id.empty()) {
            ActionRepository().finish(action_id, "failed");
            spdlog::warn("ani agent: action {} dead-lettered: {}", action_id, error.substr(0, 200));
        }
    } catch (const std::exception& e) {
        spdlog::warn("ani agent: the dead action {} was not marked failed: {}", action_id, e.what());
    }
}

}  // namespace Agent::Actions
