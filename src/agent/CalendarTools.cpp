/**
 * @file CalendarTools.cpp
 * @brief Body for src/agent/CalendarTools.hpp.
 */

#include "agent/CalendarTools.hpp"

#include <exception>
#include <utility>

#include <spdlog/spdlog.h>

#include "agent/SiteApi.hpp"
#include "agent/ToolArgs.hpp"
#include "agent/Tools.hpp"
#include "calendar/Google.hpp"
#include "repositories/calendar/LinkRepository.hpp"
#include "utils/Date.hpp"

namespace Agent::CalendarTools {

namespace {

using json = nlohmann::json;

using ToolArgs::day_arg;
using ToolArgs::tool;

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
            refusal = ToolArgs::refusal("invalid", "from and to are days, to not before from");
            return json();
        }
        return json{
            {"path", "/api/v1/calendar/events"},
            {"query", json::array({json::array({"from", from}), json::array({"to", to}), json::array({"tz", tz})})}};
    }
    if (name == "calendar_free") {
        const std::string date = day_arg(args, "date");
        if (date.empty()) {
            refusal = ToolArgs::refusal("invalid", "date is a day");
            return json();
        }
        json q = json::array({json::array({"date", date}), json::array({"tz", tz})});
        if (const auto it = args.find("minutes"); it != args.end() && it->is_number_integer()) {
            q.push_back(json::array({"min", std::to_string(it->get<long long>())}));
        }
        return json{{"path", "/api/v1/calendar/free"}, {"query", q}};
    }
    refusal = ToolArgs::refusal("unknown_tool", "there is no tool " + name);
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
        return SiteApi::tool_result(transport, api_url, access_token, req, /*keep_ids=*/true, name);
    };
}

}  // namespace Agent::CalendarTools
