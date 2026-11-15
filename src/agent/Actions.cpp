/**
 * @file Actions.cpp
 * @brief Bodies for src/agent/Actions.hpp: the table of the write tools, the
 *        runner and the job. The proposals themselves are by domain
 *        (agent/ActionKit.hpp).
 */

#include "agent/Actions.hpp"

#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <spdlog/spdlog.h>

#include "agent/ActionKit.hpp"
#include "agent/Settings.hpp"
#include "agent/SiteApi.hpp"
#include "agent/ToolArgs.hpp"
#include "agent/Tools.hpp"
#include "jobs/Job.hpp"
#include "repositories/agent/ActionRepository.hpp"
#include "utils/Utf8.hpp"

namespace Agent::Actions {

namespace {

using json = nlohmann::json;
using Repositories::Agent::ActionRepository;
using ToolArgs::refusal;

/// One write tool: its family, its definition as the gateway takes it, and
/// what a call of it becomes.
struct Spec {
    const char* name;
    Family family;
    json (*definition)();
    Proposed (*make)(const detail::Ctx&, const json&);
};

/// Every write tool, in the order the gateway is given them within a family.
const Spec kSpecs[] = {
    {"create_task", Family::Core, detail::create_task_tool, detail::create_task},
    {"complete_task", Family::Core, detail::complete_task_tool, detail::complete_task},
    {"check_habit", Family::Core, detail::check_habit_tool, detail::check_habit},
    {"add_note", Family::Core, detail::add_note_tool, detail::add_note},
    {"journal_note", Family::Core, detail::journal_note_tool, detail::journal_note},
    {"add_food", Family::Core, detail::add_food_tool, detail::add_food},
    {"add_spend", Family::Core, detail::add_spend_tool, detail::add_spend},
    {"tg_send", Family::Telegram, detail::tg_send_tool, detail::tg_send},
    {"tg_edit", Family::Telegram, detail::tg_edit_tool, detail::tg_edit},
    {"calendar_add", Family::Calendar, detail::calendar_add_tool, detail::calendar_add},
    {"calendar_move", Family::Calendar, detail::calendar_move_tool, detail::calendar_move},
    {"calendar_remove", Family::Calendar, detail::calendar_remove_tool, detail::calendar_remove},
};

const Spec* spec_of(const std::string& name) {
    for (const auto& spec : kSpecs) {
        if (name == spec.name) {
            return &spec;
        }
    }
    return nullptr;
}

json definitions_of(Family family) {
    json out = json::array();
    for (const auto& spec : kSpecs) {
        if (spec.family == family) {
            out.push_back(spec.definition());
        }
    }
    return out;
}

}  // namespace

json definitions() {
    return definitions_of(Family::Core);
}

json telegram_definitions() {
    return definitions_of(Family::Telegram);
}

json calendar_definitions() {
    return definitions_of(Family::Calendar);
}

bool is_write(const std::string& name) {
    return spec_of(name) != nullptr;
}

std::optional<Family> family_of(const std::string& name) {
    const Spec* spec = spec_of(name);
    return spec != nullptr ? std::optional<Family>(spec->family) : std::nullopt;
}

json schema_of(const std::string& name) {
    const Spec* spec = spec_of(name);
    return spec != nullptr ? spec->definition()["function"]["parameters"] : json();
}

Proposed propose(Net::Http::Transport& transport,
                 const std::string& api_url,
                 const std::string& access_token,
                 const std::string& tz,
                 const std::string& today,
                 const std::string& name,
                 const json& raw) {
    const json args = raw.is_object() ? raw : json::object();
    const Spec* spec = spec_of(name);
    if (spec == nullptr) {
        return refusal("unknown_tool", "there is no tool " + name);
    }
    try {
        return spec->make(detail::Ctx{transport, api_url, access_token, tz, today}, args);
    } catch (const Net::Http::TransportError& e) {
        spdlog::warn("ani agent: the tool {} got no answer: {}", name, e.what());
        return refusal("unavailable", "Life OS did not answer");
    }
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
    // An idempotent request (a calendar add with its own event id) is sent once more after no
    // answer: the API may have spent longer on Google than this call waits and made the change.
    const int attempts = request.value("retry", false) ? 2 : 1;
    for (int attempt = 1;; ++attempt) {
        try {
            res = transport.send(SiteApi::request_of(api_url, access_token, request));
            break;
        } catch (const Net::Http::TransportError& e) {
            spdlog::warn("ani agent: an action got no answer (try {} of {}): {}", attempt, attempts, e.what());
            if (attempt >= attempts) {
                return "unavailable";
            }
        }
    }
    if (res.status >= 200 && res.status < 300) {
        return {};
    }
    const auto body = json::parse(res.body, nullptr, /*allow_exceptions=*/false);
    if (body.is_object() && body.value("error", json()).is_string()) {
        return detail::cut(body["error"].get<std::string>(), 60);
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
