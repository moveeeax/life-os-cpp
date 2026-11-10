/**
 * @file CueTools.cpp
 * @brief Body for src/agent/CueTools.hpp.
 */

#include "agent/CueTools.hpp"

#include <algorithm>
#include <exception>
#include <utility>

#include <spdlog/spdlog.h>

#include "agent/Cues.hpp"
#include "repositories/agent/CueRepository.hpp"
#include "security/Audit.hpp"
#include "utils/Utf8.hpp"

namespace Agent::CueTools {

namespace {

using json = nlohmann::json;

json tool(const char* name, const char* description, json parameters) {
    return json{{"type", "function"},
                {"function", {{"name", name}, {"description", description}, {"parameters", std::move(parameters)}}}};
}

const json kScreen = {{"type", "string"},
                      {"enum", {"today", "food", "body", "money"}},
                      {"description", "The screen of the app the line shows on"}};

bool known_screen(const std::string& s) {
    return std::find(Cues::kScreens.begin(), Cues::kScreens.end(), s) != Cues::kScreens.end();
}

}  // namespace

json definitions() {
    return json::array({
        tool(
            "set_cue",
            "Put one short line of yours on a screen of the person's phone app for the rest of today, with an "
            "optional button (what it opens: food, spend, task, water or measure to write one, set for a set of a "
            "workout). One per screen: a new one replaces yours there. They can send it away for the day.",
            json{
                {"type", "object"},
                {"properties",
                 {{"screen", kScreen},
                  {"text", {{"type", "string"}, {"description", "The line, in their language, up to 200 characters"}}},
                  {"action",
                   {{"type", "object"},
                    {"properties",
                     {{"label", {{"type", "string"}, {"description", "The button, 1 to 30 characters"}}},
                      {"kind", {{"type", "string"}, {"enum", {"food", "spend", "task", "water", "measure", "set"}}}},
                      {"payload",
                       {{"type", "object"}, {"description", "What the button fills in, e.g. {\"meal\": \"lunch\"}"}}}}},
                    {"required", {"label", "kind"}},
                    {"additionalProperties", false}}}}},
                {"required", {"screen", "text"}},
                {"additionalProperties", false}}),
        tool("clear_cue",
             "Take your line off a screen of the app for today.",
             json{{"type", "object"},
                  {"properties", {{"screen", kScreen}}},
                  {"required", {"screen"}},
                  {"additionalProperties", false}}),
    });
}

bool is_cue(const std::string& name) {
    return name == "set_cue" || name == "clear_cue";
}

Gateway::ToolRunner runner(const std::string& owner, const std::string& tz, Gateway::ToolRunner rest) {
    return [owner, tz, rest = std::move(rest)](const std::string& name, const json& raw) -> std::string {
        if (!is_cue(name)) {
            return rest(name, raw);
        }
        const json args = raw.is_object() ? raw : json::object();
        const auto str = [&](const char* k) {
            return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string();
        };
        const std::string screen = str("screen");
        if (!known_screen(screen)) {
            return json{{"error", "invalid"}, {"message", "screen is today, food, body or money"}}.dump();
        }
        try {
            Repositories::Agent::CueRepository cues;
            if (name == "clear_cue") {
                if (!cues.clear(owner, tz, screen)) {
                    return json{{"error", "not_found"}, {"message", "no line of yours is on that screen today"}}.dump();
                }
                return json{{"cleared", true}}.dump();
            }
            const std::string text = str("text");
            const std::size_t n = Utils::Utf8::length(text);
            if (n < 1 || n > 200) {
                return json{{"error", "invalid"}, {"message", "the line is 1 to 200 characters"}}.dump();
            }
            const json action = args.contains("action") ? args["action"] : json();
            if (const auto problem = Cues::action_problem(action)) {
                return json{{"error", "invalid"}, {"message", *problem}}.dump();
            }
            const std::string id = cues.put(owner, tz, screen, text, action);
            Security::Audit::record(owner, "ani.cue.set", "ani_cue", id);
            return json{{"set", "agent-" + id}}.dump();
        } catch (const std::exception& e) {
            spdlog::warn("ani agent: the cue tool {} failed: {}", name, e.what());
            return json{{"error", "failed"}, {"message", "the line was not put"}}.dump();
        }
    };
}

}  // namespace Agent::CueTools
