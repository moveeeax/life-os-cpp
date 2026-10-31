/**
 * @file Tools.cpp
 * @brief Bodies for src/agent/Tools.hpp.
 */

#include "agent/Tools.hpp"

#include <regex>

#include <spdlog/spdlog.h>

#include "repositories/UserRepository.hpp"
#include "security/Auth.hpp"
#include "security/Jwt.hpp"
#include "storage/S3Presign.hpp"
#include "utils/Time.hpp"

namespace Agent::Tools {

namespace {

using json = nlohmann::json;

json object(json properties, json required = json::array()) {
    json schema = {{"type", "object"}, {"properties", std::move(properties)}, {"additionalProperties", false}};
    if (!required.empty()) {
        schema["required"] = std::move(required);
    }
    return schema;
}

json tool(const char* name, const char* description, json parameters) {
    return json{{"type", "function"},
                {"function", {{"name", name}, {"description", description}, {"parameters", std::move(parameters)}}}};
}

const json kDay = {{"type", "string"}, {"description", "A day as YYYY-MM-DD; today when left out"}};

/// A string argument, or empty when it is missing or not a string.
std::string text_arg(const json& args, const char* key) {
    const auto it = args.find(key);
    return it != args.end() && it->is_string() ? it->get<std::string>() : std::string();
}

/// A day argument; anything that is not YYYY-MM-DD is dropped.
std::string day_arg(const json& args, const char* key) {
    static const std::regex kDayRe("^\\d{4}-\\d{2}-\\d{2}$");
    const std::string v = text_arg(args, key);
    return std::regex_match(v, kDayRe) ? v : std::string();
}

void put(Call& c, const char* key, const std::string& value) {
    if (!value.empty()) {
        c.query.emplace_back(key, value);
    }
}

}  // namespace

json definitions() {
    return json::array({
        tool("today",
             "The person's day on the Now page: tasks, goals, food, money, health, travel, habits and the focus.",
             object({{"date", kDay}})),
        tool("tasks_of_day",
             "Tasks of a day: overdue, due that day, dated later, someday, and done that day.",
             object({{"date", kDay}})),
        tool("find_tasks",
             "Search tasks by words of their title; optionally by status.",
             object({{"query", {{"type", "string"}, {"description", "Words of the title"}}},
                     {"status", {{"type", "string"}, {"enum", {"open", "in_progress", "done"}}}}})),
        tool("goals",
             "Goals with their progress; active ones unless a status is given.",
             object({{"status", {{"type", "string"}, {"enum", {"active", "done", "dropped"}}}}})),
        tool("habits", "The person's habits in their order.", object(json::object())),
        tool("sleep",
             "Sleep sessions from the person's watch between two days (both included).",
             object({{"from", {{"type", "string"}, {"description", "First day, YYYY-MM-DD"}}},
                     {"to", {{"type", "string"}, {"description", "Last day, YYYY-MM-DD"}}}},
                    json::array({"from", "to"}))),
        tool("money",
             "Income, spending and the categories of a week, month or quarter, per currency.",
             object({{"period", {{"type", "string"}, {"enum", {"week", "month", "quarter"}}}},
                     {"date", {{"type", "string"}, {"description", "A day inside the period; today when left out"}}}})),
        tool("food",
             "The food diary of a day with its totals, targets and the water drunk (ml).",
             object({{"date", kDay}})),
    });
}

std::optional<Call> route(const std::string& name, const json& raw, const std::string& tz) {
    const json args = raw.is_object() ? raw : json::object();
    Call c;
    if (name == "today") {
        c.path = "/api/v1/today";
        put(c, "date", day_arg(args, "date"));
    } else if (name == "tasks_of_day") {
        c.path = "/api/v1/tasks/agenda";
        put(c, "date", day_arg(args, "date"));
    } else if (name == "find_tasks") {
        c.path = "/api/v1/tasks/items";
        c.query.emplace_back("limit", "20");
        put(c, "q", text_arg(args, "query").substr(0, 200));
        put(c, "status", text_arg(args, "status"));
    } else if (name == "goals") {
        c.path = "/api/v1/goals";
        const std::string status = text_arg(args, "status");
        put(c, "status", status.empty() ? "active" : status);
    } else if (name == "habits") {
        c.path = "/api/v1/habits";
    } else if (name == "sleep") {
        c.path = "/api/v1/fitness/sleep";
        put(c, "from", day_arg(args, "from"));
        put(c, "to", day_arg(args, "to"));
    } else if (name == "money") {
        c.path = "/api/v1/money/reports/period";
        const std::string period = text_arg(args, "period");
        put(c, "kind", period.empty() ? "month" : period);
        put(c, "date", day_arg(args, "date"));
    } else if (name == "food") {
        c.path = "/api/v1/food/day";
        put(c, "date", day_arg(args, "date"));
    } else {
        return std::nullopt;
    }
    c.query.emplace_back("tz", tz);
    return c;
}

json compact(const json& v) {
    if (v.is_array()) {
        json out = json::array();
        for (const auto& x : v) {
            out.push_back(compact(x));
        }
        return out;
    }
    if (v.is_object()) {
        json out = json::object();
        for (const auto& [k, x] : v.items()) {
            const bool id = k == "id" || (k.size() > 3 && k.compare(k.size() - 3, 3, "_id") == 0);
            if (x.is_null() || id || k == "errors" || (x.is_array() && x.empty())) {
                continue;
            }
            out[k] = compact(x);
        }
        return out;
    }
    return v;
}

std::string result_text(const Net::Http::Response& r) {
    const auto body = json::parse(r.body, nullptr, /*allow_exceptions=*/false);
    if (r.status == 200 && body.is_object()) {
        return compact(body.contains("data") ? body["data"] : body).dump();
    }
    if (r.status == 404) {
        return json{{"error", "not_available"}, {"message", "this part of Life OS is off for the person"}}.dump();
    }
    json err = {{"error", body.is_object() ? body.value("error", json("failed")) : json("failed")},
                {"status", r.status}};
    if (body.is_object() && body.value("message", json()).is_string()) {
        err["message"] = body["message"];
    }
    return err.dump();
}

Gateway::ToolRunner runner(Net::Http::Transport& transport,
                           const std::string& api_url,
                           const std::string& access_token,
                           const std::string& tz) {
    return [&transport, api_url, access_token, tz](const std::string& name, const json& args) -> std::string {
        const auto call = route(name, args, tz);
        if (!call) {
            return json{{"error", "unknown_tool"}, {"message", "there is no tool " + name}}.dump();
        }
        std::string url = api_url + call->path;
        char sep = '?';
        for (const auto& [k, v] : call->query) {
            url += sep + k + "=" + Storage::S3::uri_encode(v, true);
            sep = '&';
        }
        Net::Http::Request r;
        r.url = url;
        r.headers = {{"Authorization", "Bearer " + access_token}, {"Accept", "application/json"}};
        r.timeout_seconds = 20;
        try {
            return result_text(transport.send(r));
        } catch (const Net::Http::TransportError& e) {
            spdlog::warn("ani agent: the tool {} got no answer: {}", name, e.what());
            return json{{"error", "unavailable"}, {"message", "Life OS did not answer"}}.dump();
        }
    };
}

std::optional<std::string> access_token(const std::string& owner) {
    const auto cfg = Security::Auth::load_config_from_global();
    if (cfg.jwt_secret.empty()) {
        return std::nullopt;
    }
    const auto user = Repositories::UserRepository().find(owner);
    if (!user) {
        return std::nullopt;
    }
    Security::Auth::AccessSubject who;
    who.id = user->id;
    who.confirmed = user->confirmed;
    who.permissions = user->role ? user->role->permissions : 0U;
    if (user->role) {
        who.roles.push_back(user->role->name);
    }
    const auto claims = Security::Auth::access_claims(
        who, Utils::Time::now_epoch_millis(), kTokenSeconds, cfg.jwt_roles_claim, cfg.jwt_issuer, cfg.jwt_audience);
    return Security::Auth::issue_hs256_jwt(claims, cfg.jwt_secret);
}

}  // namespace Agent::Tools
