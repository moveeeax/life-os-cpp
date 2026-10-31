/**
 * @file DealTools.cpp
 * @brief Body for src/agent/DealTools.hpp.
 */

#include "agent/DealTools.hpp"

#include <exception>
#include <optional>
#include <utility>

#include <spdlog/spdlog.h>

#include "repositories/agent/DealRepository.hpp"
#include "security/Audit.hpp"

namespace Agent::DealTools {

namespace {

using json = nlohmann::json;

json tool(const char* name, const char* description, json parameters) {
    return json{{"type", "function"},
                {"function", {{"name", name}, {"description", description}, {"parameters", std::move(parameters)}}}};
}

std::size_t length(const std::string& s) {
    std::size_t n = 0;
    for (const unsigned char c : s) {
        n += (c & 0xC0) != 0x80 ? 1 : 0;
    }
    return n;
}

std::string unit_of(const json& d) {
    const std::string kind = d.value("kind", std::string());
    if (kind == "workouts") {
        return "workouts";
    }
    if (kind == "focus_minutes") {
        return "focus minutes";
    }
    return "ticks of " + d.value("habit", std::string("a habit"));
}

}  // namespace

json definitions() {
    return json::array({
        tool("promise",
             "Keep a promise the person makes to you for this week (Monday to Sunday), when they say it plainly: "
             "\"three workouts\", \"five hours of focus\", \"meditation every day\". It is checked against their "
             "data; hold them to it midweek and praise it when kept.",
             json{{"type", "object"},
                  {"properties",
                   {{"text", {{"type", "string"}, {"description", "The promise in their words, one short sentence"}}},
                    {"kind",
                     {{"type", "string"},
                      {"enum", {"workouts", "focus_minutes", "habit"}},
                      {"description", "What proves it: finished workouts, minutes of focus, or ticks of a habit"}}},
                    {"target", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10000}}},
                    {"habit", {{"type", "string"}, {"description", "The habit's name, for kind habit"}}}}},
                  {"required", {"text", "kind", "target"}},
                  {"additionalProperties", false}}),
        tool("drop_promise",
             "Drop a promise of this week, by its id from the list at the start of the message, when the person "
             "takes it back.",
             json{{"type", "object"},
                  {"properties", {{"id", {{"type", "string"}}}}},
                  {"required", {"id"}},
                  {"additionalProperties", false}}),
    });
}

bool is_deal(const std::string& name) {
    return name == "promise" || name == "drop_promise";
}

Gateway::ToolRunner runner(const std::string& owner, const std::string& tz, Gateway::ToolRunner rest) {
    return [owner, tz, rest = std::move(rest)](const std::string& name, const json& raw) -> std::string {
        if (!is_deal(name)) {
            return rest(name, raw);
        }
        const json args = raw.is_object() ? raw : json::object();
        const auto str = [&](const char* k) {
            return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string();
        };
        try {
            Repositories::Agent::DealRepository deals;
            if (name == "promise") {
                const std::string text = str("text");
                const std::string kind = str("kind");
                const long long target =
                    args.contains("target") && args["target"].is_number_integer() ? args["target"].get<long long>() : 0;
                if (text.empty() || length(text) > 200) {
                    return json{{"error", "invalid"}, {"message", "the promise is 1 to 200 characters"}}.dump();
                }
                if (kind != "workouts" && kind != "focus_minutes" && kind != "habit") {
                    return json{{"error", "invalid"}, {"message", "kind is workouts, focus_minutes or habit"}}.dump();
                }
                if (target < 1 || target > 10000) {
                    return json{{"error", "invalid"}, {"message", "target is 1 to 10000"}}.dump();
                }
                std::optional<std::string> habit;
                if (kind == "habit") {
                    habit = deals.habit_named(owner, str("habit"));
                    if (!habit) {
                        return json{{"error", "unknown_habit"}, {"message", "no habit of theirs has that name"}}.dump();
                    }
                }
                const auto id = deals.add(owner, tz, text, kind, static_cast<int>(target), habit);
                if (!id) {
                    return json{{"error", "too_many"},
                                {"message", "this week holds five promises already: drop one first"}}
                        .dump();
                }
                Security::Audit::record(owner, "ani.deal.add", "ani_deal", *id);
                return json{{"promised", *id}}.dump();
            }
            const std::string id = str("id");
            if (!deals.remove(owner, id)) {
                return json{{"error", "not_found"}, {"message", "no promise of this week has that id"}}.dump();
            }
            Security::Audit::record(owner, "ani.deal.remove", "ani_deal", id);
            return json{{"dropped", true}}.dump();
        } catch (const std::exception& e) {
            spdlog::warn("ani agent: the deal tool {} failed: {}", name, e.what());
            return json{{"error", "failed"}, {"message", "it was not kept"}}.dump();
        }
    };
}

std::string deals_note(const json& deals) {
    if (!deals.is_array() || deals.empty()) {
        return {};
    }
    std::string out = "[Promises of this week (promise adds, drop_promise drops by id):";
    for (const auto& d : deals) {
        out += " " + d.value("id", std::string()) + ": " + d.value("text", std::string()) + " (" +
               std::to_string(d.value("done", 0LL)) + " of " + std::to_string(d.value("target", 0LL)) + " " +
               unit_of(d) + ");";
    }
    return out + "]";
}

}  // namespace Agent::DealTools
