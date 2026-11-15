/**
 * @file Toolset.cpp
 * @brief Body for src/agent/Toolset.hpp.
 */

#include "agent/Toolset.hpp"

#include <functional>
#include <utility>
#include <vector>

#include "agent/Actions.hpp"
#include "agent/CalendarTools.hpp"
#include "agent/CueTools.hpp"
#include "agent/DealTools.hpp"
#include "agent/FactTools.hpp"
#include "agent/TelegramTools.hpp"
#include "agent/Tools.hpp"
#include "net/Http.hpp"

namespace Agent {

namespace {

using json = nlohmann::json;

/// One family of tools: whether the turn has it, its definitions, and its
/// runner, which answers its own tools and hands every other call to the
/// runner it wraps.
struct Family {
    bool available;
    json definitions;
    std::function<Gateway::ToolRunner(Gateway::ToolRunner rest)> wrap;
};

/// The write tools of the families the turn has.
json write_definitions(bool telegram, bool calendar) {
    json out = Actions::definitions();
    if (telegram) {
        for (const auto& t : Actions::telegram_definitions()) {
            out.push_back(t);
        }
    }
    if (calendar) {
        for (const auto& t : Actions::calendar_definitions()) {
            out.push_back(t);
        }
    }
    return out;
}

}  // namespace

Toolset build_toolset(const std::string& api_url,
                      const std::string& owner,
                      const std::string& tz,
                      const std::string& local,
                      const std::string& turn_id,
                      const std::string& origin) {
    Toolset out{json::array(), [](const std::string&, const json&) { return json{{"error", "unavailable"}}.dump(); }};
    if (api_url.empty()) {
        return out;
    }
    const auto token = Tools::access_token(owner);
    if (!token) {
        return out;
    }
    Net::Http::Transport& transport = Net::Http::transport();
    out.tools = Tools::definitions();
    out.run = Tools::runner(transport, api_url, *token, tz);
    // The owner's Telegram, for the one user it serves.
    const bool telegram = TelegramTools::available(owner);
    const bool calendar = CalendarTools::available(owner);
    // The families in the order the gateway sees their tools; each wraps the runner before it.
    const std::vector<Family> families = {
        {telegram,
         TelegramTools::definitions(),
         [&](Gateway::ToolRunner rest) { return TelegramTools::runner(transport, api_url, *token, std::move(rest)); }},
        {true,
         FactTools::definitions(),
         [&](Gateway::ToolRunner rest) { return FactTools::runner(owner, std::move(rest)); }},
        {true,
         DealTools::definitions(),
         [&](Gateway::ToolRunner rest) { return DealTools::runner(owner, tz, std::move(rest)); }},
        {true,
         CueTools::definitions(),
         [&](Gateway::ToolRunner rest) { return CueTools::runner(owner, tz, std::move(rest)); }},
        {calendar,
         CalendarTools::definitions(),
         [&](Gateway::ToolRunner rest) {
             return CalendarTools::runner(transport, api_url, *token, tz, std::move(rest));
         }},
        // Changes need the person's day (a habit is ticked for today or before).
        {local.size() >= 10,
         write_definitions(telegram, calendar),
         [&](Gateway::ToolRunner rest) {
             return Actions::runner(
                 transport, api_url, *token, tz, local.substr(0, 10), owner, turn_id, std::move(rest));
         }},
    };
    for (const auto& family : families) {
        if (!family.available) {
            continue;
        }
        for (const auto& t : family.definitions) {
            out.tools.push_back(t);
        }
        out.run = family.wrap(std::move(out.run));
    }
    // A webhook turn reads and proposes only; an MCP one leaves Telegram alone.
    out.tools = Tools::offered(origin, out.tools);
    out.run = Tools::offered_runner(origin, std::move(out.run));
    return out;
}

}  // namespace Agent
