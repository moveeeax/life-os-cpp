/**
 * @file Turn.cpp
 * @brief Bodies for src/agent/Turn.hpp.
 */

#include "agent/Turn.hpp"

#include <exception>
#include <utility>

#include <spdlog/spdlog.h>

#include "agent/Actions.hpp"
#include "agent/Gateway.hpp"
#include "agent/Lifecycle.hpp"
#include "agent/Manifest.hpp"
#include "agent/Settings.hpp"
#include "agent/Tools.hpp"
#include "jobs/Job.hpp"
#include "jobs/Jobs.hpp"
#include "net/Http.hpp"
#include "repositories/agent/ActionRepository.hpp"
#include "repositories/agent/AgentRepository.hpp"
#include "repositories/agent/TurnRepository.hpp"

namespace Agent {

namespace {

using json = nlohmann::json;
using Repositories::Agent::ActionRepository;
using Repositories::Agent::AgentRepository;
using Repositories::Agent::TurnRepository;

bool over_budget(AgentRepository& agents, const std::string& owner, long long budget) {
    const json used = agents.month_usage(owner);
    return used.value("prompt_tokens", 0LL) + used.value("completion_tokens", 0LL) >= budget;
}

json ended(const std::string& id, const std::string& owner, const std::string& error, const std::string& why) {
    spdlog::warn("ani agent {}: turn {} failed ({}): {}", Manifest::owner_hash(owner), id, error, why);
    TurnRepository().fail(id, error);
    return json{{"status", "failed"}, {"error", error}};
}

/// What became of a proposal, as the agent is told.
std::string outcome_words(const std::string& status) {
    if (status == "done") {
        return "done";
    }
    if (status == "failed") {
        return "failed";
    }
    if (status == "cancelled") {
        return "cancelled by the person";
    }
    if (status == "expired") {
        return "never confirmed";
    }
    if (status == "pending") {
        return "still waiting for the person";
    }
    return "being done";
}

}  // namespace

std::string session_of(const std::string& owner, int chat_session) {
    return chat_session == 0 ? owner : owner + "#" + std::to_string(chat_session);
}

std::string preface(const std::string& local, const std::string& tz, const json& tools, const json& outcomes) {
    std::string out = "[Life OS: ";
    if (!local.empty()) {
        out += "the person's local time is " + local + ", " + tz + ". ";
    }
    out += "Reply in one or two short spoken sentences.";
    std::string reads;
    std::string writes;
    for (const auto& t : tools.is_array() ? tools : json::array()) {
        const std::string name = t.value(json::json_pointer("/function/name"), std::string());
        std::string& names = Actions::is_write(name) ? writes : reads;
        names += (names.empty() ? "" : ", ") + name;
    }
    if (!reads.empty()) {
        // Left to itself the model answers from its memory of the person; it
        // calls the tools every time once told the data is not there.
        out +=
            " Their Life OS data (day, tasks, goals, habits, sleep, money, food) is not in your memory: read it "
            "with the tools of this request (" +
            reads + ") every time it is asked about, never from memory.";
    }
    if (!writes.empty()) {
        out += " Changes (" + writes +
               ") are only proposed: the person confirms each with a button under your reply, so never say one "
               "is done.";
    }
    if (outcomes.is_array() && !outcomes.empty()) {
        out += " Your proposals of the last message:";
        for (const auto& o : outcomes) {
            out += " " + o.value("summary", std::string()) + ": " + outcome_words(o.value("status", "")) + ".";
        }
    }
    return out + "]";
}

Sent send(const std::string& owner, const std::string& text, const std::string& tz) {
    const auto s = read_settings();
    if (!s) {
        return {Send::Off, json()};
    }
    AgentRepository agents;
    if (over_budget(agents, owner, s->monthly_tokens)) {
        return {Send::Budget, json()};
    }
    // A sleeping agent must find a slot; one already up is touched by the wake.
    if (wake(owner) == Wake::Busy) {
        return {Send::Busy, json()};
    }
    auto turn = TurnRepository().open(owner, text, tz);
    if (!turn) {
        return {Send::Open, json()};
    }
    Jobs::get().submit(kTurnJob, json{{"turn", (*turn)["id"]}});
    return {Send::Queued, std::move(*turn)};
}

json process_turn(const json& payload) {
    const std::string id = payload.value("turn", std::string());
    if (id.empty()) {
        throw Jobs::PermanentJobError("ani turn job without a turn");
    }
    TurnRepository turns;
    const auto turn = turns.for_worker(id);
    if (!turn) {
        return json{{"status", "skipped"}, {"reason", "no_turn"}};
    }
    const std::string owner = turn->value("owner_id", "");
    const std::string status = turn->value("status", "");
    if (status == "running") {
        // A worker died inside this turn: running it again could repeat what it did.
        return ended(id, owner, "failed", "interrupted");
    }
    if (status != "queued") {
        return json{{"status", "skipped"}, {"reason", status}};
    }

    try {
        const auto s = read_settings();
        if (!s) {
            return ended(id, owner, "unavailable", "the agent is not configured");
        }
        AgentRepository agents;
        if (over_budget(agents, owner, s->monthly_tokens)) {
            return ended(id, owner, "budget", "the month's tokens are spent");
        }
        const auto row = agents.get(owner);
        const std::string state = row ? row->value("state", "stopped") : "stopped";
        if (state != "running") {
            if (turn->value("age_seconds", 0LL) > kTurnWaitSeconds) {
                return ended(id, owner, "timeout", "the agent did not come up (" + state + ")");
            }
            // A stopped agent (it went idle, or a stop finished meanwhile) is woken
            // again; starting and stopping ones are waited on.
            if (state == "stopped" && wake(owner) == Wake::Busy) {
                return ended(id, owner, "busy", "every agent slot is taken");
            }
            throw Jobs::DeferJob("waiting for the agent (" + state + ")", 2000);
        }
        const auto gateway = agents.gateway(owner);
        if (!gateway) {
            throw Jobs::DeferJob("waiting for the agent's address", 2000);
        }
        if (!turns.start(id)) {
            return json{{"status", "skipped"}, {"reason", "not_queued"}};
        }
        agents.touch(owner);

        const std::string tz = turn->value("tz", "UTC");
        const std::string local = turns.local_now(tz).value_or("");
        json tools = json::array();
        Gateway::ToolRunner run = [](const std::string&, const json&) { return json{{"error", "unavailable"}}.dump(); };
        if (!s->api_url.empty()) {
            if (const auto token = Tools::access_token(owner)) {
                tools = Tools::definitions();
                run = Tools::runner(Net::Http::transport(), s->api_url, *token, tz);
                // Changes need the person's day (a habit is ticked for today or before).
                if (local.size() >= 10) {
                    for (const auto& t : Actions::definitions()) {
                        tools.push_back(t);
                    }
                    run = Actions::runner(
                        Net::Http::transport(), s->api_url, *token, tz, local.substr(0, 10), owner, id, std::move(run));
                }
            }
        }
        const json outcomes = ActionRepository().of_previous_turn(owner, id);
        // The gateway ignores system messages: the context goes in front of the text.
        const std::string text = preface(local, tz, tools, outcomes) + "\n" + turn->value("text", "");
        const auto out = Gateway::converse(Net::Http::transport(),
                                           "http://" + gateway->first + ":" + std::to_string(Gateway::kGatewayPort),
                                           gateway->second,
                                           session_of(owner, agents.chat_session(owner)),
                                           text,
                                           tools,
                                           run);
        turns.finish(id, out.reply, out.tools);
        agents.touch(owner);
        return json{{"status", "done"}, {"tools", out.tools.size()}};
    } catch (const Jobs::DeferJob&) {
        throw;
    } catch (const Gateway::Error& e) {
        // Woken just now, the agent may still be preparing: the turn waits for it.
        if (e.not_ready && turn->value("age_seconds", 0LL) <= kTurnWaitSeconds && turns.requeue(id)) {
            throw Jobs::DeferJob("the agent is still starting", 3000);
        }
        return ended(id, owner, e.code, e.what());
    } catch (const std::exception& e) {
        return ended(id, owner, "failed", e.what());
    }
}

void fail_turn(const std::string& turn_id, const std::string& error) {
    try {
        if (!turn_id.empty()) {
            TurnRepository().fail(turn_id, "failed");
            spdlog::warn("ani agent: turn {} dead-lettered: {}", turn_id, error.substr(0, 200));
        }
    } catch (const std::exception& e) {
        spdlog::warn("ani agent: the dead turn {} was not marked failed: {}", turn_id, e.what());
    }
}

}  // namespace Agent
