/**
 * @file FactTools.cpp
 * @brief Body for src/agent/FactTools.hpp.
 */

#include "agent/FactTools.hpp"

#include <exception>
#include <utility>

#include <spdlog/spdlog.h>

#include "repositories/agent/FactRepository.hpp"
#include "security/Audit.hpp"
#include "utils/Utf8.hpp"

namespace Agent::FactTools {

namespace {

using json = nlohmann::json;

json tool(const char* name, const char* description, json parameters) {
    return json{{"type", "function"},
                {"function", {{"name", name}, {"description", description}, {"parameters", std::move(parameters)}}}};
}

using Utils::Utf8::length;

}  // namespace

json definitions() {
    return json::array({
        tool("remember",
             "Keep one short fact about the person (a preference, a routine, someone close). They see and edit the "
             "list in their account; do not keep secrets, health details or other people's data.",
             json{{"type", "object"},
                  {"properties", {{"fact", {{"type", "string"}, {"description", "The fact, one sentence"}}}}},
                  {"required", {"fact"}},
                  {"additionalProperties", false}}),
        tool("forget",
             "Drop a fact you keep, by its id from the list at the start of the message.",
             json{{"type", "object"},
                  {"properties", {{"id", {{"type", "string"}}}}},
                  {"required", {"id"}},
                  {"additionalProperties", false}}),
    });
}

bool is_fact(const std::string& name) {
    return name == "remember" || name == "forget";
}

Gateway::ToolRunner runner(const std::string& owner, Gateway::ToolRunner rest) {
    return [owner, rest = std::move(rest)](const std::string& name, const json& raw) -> std::string {
        if (!is_fact(name)) {
            return rest(name, raw);
        }
        const json args = raw.is_object() ? raw : json::object();
        try {
            Repositories::Agent::FactRepository facts;
            if (name == "remember") {
                const std::string fact = args.value("fact", std::string());
                if (fact.empty() || length(fact) > 200) {
                    return json{{"error", "invalid"}, {"message", "a fact is 1 to 200 characters"}}.dump();
                }
                const auto id = facts.add(owner, fact, "chat");
                if (!id) {
                    return json{{"error", "too_many"}, {"message", "50 facts are kept already: forget one first"}}
                        .dump();
                }
                Security::Audit::record(owner, "ani.fact.add", "ani_fact", *id);
                return json{{"remembered", *id}}.dump();
            }
            const std::string id = args.value("id", std::string());
            if (id.empty() || !facts.remove(owner, id)) {
                return json{{"error", "not_found"}, {"message", "there is no such fact"}}.dump();
            }
            Security::Audit::record(owner, "ani.fact.remove", "ani_fact", id);
            return json{{"forgotten", true}}.dump();
        } catch (const std::exception& e) {
            spdlog::warn("ani agent: the fact tool {} failed: {}", name, e.what());
            return json{{"error", "failed"}, {"message", "it was not kept"}}.dump();
        }
    };
}

std::string facts_note(const json& facts) {
    if (!facts.is_array() || facts.empty()) {
        return {};
    }
    std::string out = "[Facts the person lets you keep (they see them; remember adds, forget drops by id):";
    for (const auto& f : facts) {
        out += " " + f.value("id", std::string()) + ": " + f.value("text", std::string()) + ";";
    }
    return out + "]";
}

}  // namespace Agent::FactTools
