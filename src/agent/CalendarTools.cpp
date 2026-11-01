/**
 * @file CalendarTools.cpp
 * @brief Body for src/agent/CalendarTools.hpp.
 */

#include "agent/CalendarTools.hpp"

#include <exception>
#include <utility>

#include <spdlog/spdlog.h>

#include "agent/Tools.hpp"
#include "calendar/Google.hpp"
#include "repositories/calendar/LinkRepository.hpp"
#include "storage/S3Presign.hpp"
#include "utils/Date.hpp"

namespace Agent::CalendarTools {

namespace {

using json = nlohmann::json;

json tool(const char* name, const char* description, json parameters) {
    return json{{"type", "function"},
                {"function", {{"name", name}, {"description", description}, {"parameters", std::move(parameters)}}}};
}

std::string day_arg(const json& args, const char* key) {
    const auto it = args.find(key);
    const std::string v = it != args.end() && it->is_string() ? it->get<std::string>() : std::string();
    return Utils::Date::is_ymd(v) ? v : std::string();
}

}  // namespace

bool available(const std::string& owner) {
    try {
        if (!::Calendar::settings()) {
            return false;
        }
        const auto link = Repositories::Calendar::LinkRepository().get(owner);
        return link && !link->broken;
    } catch (const std::exception& e) {
        spdlog::warn("ani agent: the calendar link was not read: {}", e.what());
        return false;
    }
}

json definitions() {
    const json day = {{"type", "string"}, {"description", "A day as YYYY-MM-DD"}};
    return json::array({
        tool("calendar_events",
             "The person's Google Calendar events of a range of days (both included, at most 31), with their ids.",
             json{{"type", "object"},
                  {"properties", {{"from", day}, {"to", day}}},
                  {"required", {"from", "to"}},
                  {"additionalProperties", false}}),
        tool("calendar_free",
             "Free slots of a day between 08:00 and 22:00 in the person's zone, at least some minutes long.",
             json{{"type", "object"},
                  {"properties",
                   {{"date", day},
                    {"minutes", {{"type", "integer"}, {"description", "Shortest slot, 5 to 480; 30 when left out"}}}}},
                  {"required", {"date"}},
                  {"additionalProperties", false}}),
    });
}

bool is_calendar(const std::string& name) {
    return name == "calendar_events" || name == "calendar_free";
}

json request_of(const std::string& name, const json& raw, const std::string& tz, std::string& refusal) {
    const json args = raw.is_object() ? raw : json::object();
    if (name == "calendar_events") {
        const std::string from = day_arg(args, "from");
        const std::string to = day_arg(args, "to");
        if (from.empty() || to.empty() || to < from) {
            refusal = json{{"error", "invalid"}, {"message", "from and to are days, to not before from"}}.dump();
            return json();
        }
        return json{
            {"path", "/api/v1/calendar/events"},
            {"query", json::array({json::array({"from", from}), json::array({"to", to}), json::array({"tz", tz})})}};
    }
    if (name == "calendar_free") {
        const std::string date = day_arg(args, "date");
        if (date.empty()) {
            refusal = json{{"error", "invalid"}, {"message", "date is a day"}}.dump();
            return json();
        }
        json q = json::array({json::array({"date", date}), json::array({"tz", tz})});
        if (const auto it = args.find("minutes"); it != args.end() && it->is_number_integer()) {
            q.push_back(json::array({"min", std::to_string(it->get<long long>())}));
        }
        return json{{"path", "/api/v1/calendar/free"}, {"query", q}};
    }
    refusal = json{{"error", "unknown_tool"}, {"message", "there is no tool " + name}}.dump();
    return json();
}

Gateway::ToolRunner runner(Net::Http::Transport& transport,
                           const std::string& api_url,
                           const std::string& access_token,
                           const std::string& tz,
                           Gateway::ToolRunner rest) {
    return [&transport, api_url, access_token, tz, rest = std::move(rest)](const std::string& name,
                                                                           const json& args) -> std::string {
        if (!is_calendar(name)) {
            return rest(name, args);
        }
        std::string refusal;
        const json req = request_of(name, args, tz, refusal);
        if (!refusal.empty()) {
            return refusal;
        }
        std::string url = api_url + req.value("path", std::string());
        char sep = '?';
        for (const auto& kv : req.value("query", json::array())) {
            url += sep + kv[0].get<std::string>() + "=" + Storage::S3::uri_encode(kv[1].get<std::string>(), true);
            sep = '&';
        }
        Net::Http::Request r;
        r.url = url;
        r.headers = {{"Authorization", "Bearer " + access_token}, {"Accept", "application/json"}};
        r.timeout_seconds = 20;
        try {
            const auto res = transport.send(r);
            const auto body = json::parse(res.body, nullptr, /*allow_exceptions=*/false);
            if (res.status == 200 && body.is_object() && body.contains("data")) {
                return body["data"].dump();
            }
            return Tools::result_text(res);
        } catch (const Net::Http::TransportError& e) {
            spdlog::warn("ani agent: the tool {} got no answer: {}", name, e.what());
            return json{{"error", "unavailable"}, {"message", "Life OS did not answer"}}.dump();
        }
    };
}

}  // namespace Agent::CalendarTools
