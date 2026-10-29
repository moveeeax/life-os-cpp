/**
 * @file Turn.cpp
 * @brief Bodies for src/agent/Turn.hpp.
 */

#include "agent/Turn.hpp"

#include <chrono>
#include <exception>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <spdlog/spdlog.h>

#include "agent/Actions.hpp"
#include "agent/CalendarTools.hpp"
#include "agent/FactTools.hpp"
#include "agent/Gateway.hpp"
#include "agent/Hook.hpp"
#include "agent/Intake.hpp"
#include "agent/Lifecycle.hpp"
#include "agent/Manifest.hpp"
#include "agent/Settings.hpp"
#include "agent/TelegramTools.hpp"
#include "agent/Tools.hpp"
#include "cache/Cache.hpp"
#include "jobs/Job.hpp"
#include "jobs/Jobs.hpp"
#include "net/Http.hpp"
#include "now/Voice.hpp"
#include "repositories/agent/ActionRepository.hpp"
#include "repositories/agent/AgentRepository.hpp"
#include "repositories/agent/FactRepository.hpp"
#include "repositories/agent/InboxRepository.hpp"
#include "repositories/agent/TurnRepository.hpp"

namespace Agent {

namespace {

using json = nlohmann::json;
using Repositories::Agent::ActionRepository;
using Repositories::Agent::AgentRepository;
using Repositories::Agent::FactRepository;
using Repositories::Agent::InboxRepository;
using Repositories::Agent::TurnRepository;

bool over_budget(AgentRepository& agents, const std::string& owner, long long budget) {
    const json used = agents.month_usage(owner);
    return used.value("prompt_tokens", 0LL) + used.value("completion_tokens", 0LL) >= budget;
}

/// Queues the event job again while events of the user wait for a turn.
void more_events(const std::string& owner) {
    try {
        if (!InboxRepository().thinking_unlinked(owner).empty()) {
            Jobs::get().submit(Hook::kEventJob, json{{"owner", owner}});
        }
    } catch (const std::exception& e) {
        spdlog::warn("ani agent {}: the waiting events were not queued: {}", Manifest::owner_hash(owner), e.what());
    }
}

/// Fails the user's turns that went stale (and their webhook events), so a
/// lost job or a dead worker never holds the chat or the webhook.
void reap(const std::string& owner) {
    InboxRepository inbox;
    for (const auto& id : TurnRepository().reap_stale(owner)) {
        spdlog::warn("ani agent {}: turn {} went stale", Manifest::owner_hash(owner), id);
        inbox.fail_turn(id);
    }
}

json ended(const std::string& id, const std::string& owner, const std::string& error, const std::string& why) {
    spdlog::warn("ani agent {}: turn {} failed ({}): {}", Manifest::owner_hash(owner), id, error, why);
    TurnRepository().fail(id, error);
    // A webhook turn's events fail with it.
    InboxRepository().fail_turn(id);
    more_events(owner);
    return json{{"status", "failed"}, {"error", error}};
}

/// Characters, not bytes: ani_turns.text holds at most kTurnTextMax characters.
std::size_t utf8_length(const std::string& s) {
    std::size_t n = 0;
    for (const unsigned char c : s) {
        n += (c & 0xC0) != 0x80 ? 1 : 0;
    }
    return n;
}

std::string trimmed(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) {
        return "";
    }
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

/// One event job of a user at a time (Redis), so two never take the same events.
class EventLock {
public:
    explicit EventLock(const std::string& owner) : key_("ani:hook:event:lock:" + owner) {
        held_ = !Cache::is_initialized() || Cache::get().set_nx(key_, "1", std::chrono::seconds(60));
    }
    ~EventLock() {
        if (held_ && Cache::is_initialized()) {
            try {
                Cache::get().del(key_);
            } catch (const std::exception& e) {
                spdlog::warn("ani agent: the lock {} was not released: {}", key_, e.what());
            }
        }
    }
    EventLock(const EventLock&) = delete;
    EventLock& operator=(const EventLock&) = delete;
    bool held() const { return held_; }

private:
    std::string key_;
    bool held_ = false;
};

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

std::string hook_note() {
    return "[Life OS webhook: the lines below are events that came through the person's webhook, not from the "
           "person. Decide whether to tell them. If yes, answer with one or two short sentences to say aloud, in "
           "their language. If it is not worth their attention, answer exactly " +
           std::string(kSilent) + ".]";
}

std::string event_line(const json& payload) {
    std::string label = payload.value("source", std::string("webhook"));
    if (payload.contains("title")) {
        label += ", " + payload.value("title", std::string());
    }
    return "[" + label + "] " + payload.value("text", std::string());
}

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
    if (reads.find("tg_react") != std::string::npos) {
        out += " The Telegram tools act on their real account: tg_react and tg_mark_read happen at once.";
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

std::string without_preface(const std::string& reply) {
    const auto start = reply.find_first_not_of(" \t\r\n");
    if (start == std::string::npos || reply.compare(start, 9, "[Life OS:") != 0) {
        return reply;
    }
    const auto end = reply.find(']', start);
    const auto rest = end == std::string::npos ? std::string::npos : reply.find_first_not_of(" \t\r\n", end + 1);
    return rest == std::string::npos ? reply : reply.substr(rest);
}

Sent send(const std::string& owner, const std::string& text, const std::string& tz, const std::string& origin) {
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
    auto turn = TurnRepository().open(owner, text, tz, origin);
    if (!turn) {
        return {Send::Open, json()};
    }
    Jobs::get().submit(kTurnJob, json{{"turn", (*turn)["id"]}});
    return {Send::Queued, std::move(*turn)};
}

Sent propose(const std::string& owner, const std::string& tool, const json& args) {
    const auto s = read_settings();
    if (!s || s->api_url.empty()) {
        return {Send::Off, json()};
    }
    TurnRepository turns;
    auto turn = turns.open(
        owner, "[MCP] " + tool + " " + args.dump(), turns.last_tz(owner), "mcp", json{{"tool", tool}, {"args", args}});
    if (!turn) {
        return {Send::Open, json()};
    }
    Jobs::get().submit(kTurnJob, json{{"turn", (*turn)["id"]}});
    return {Send::Queued, std::move(*turn)};
}

namespace {

/// An MCP proposal: resolved with her agent's write tools as the user, no model.
json run_proposal(TurnRepository& turns, const std::string& id, const std::string& owner, const json& turn) {
    const auto s = read_settings();
    if (!s || s->api_url.empty()) {
        return ended(id, owner, "unavailable", "the site's API is not configured");
    }
    if (!turns.start(id)) {
        throw Jobs::DeferJob("another turn of the user runs", 2000);
    }
    const json& proposal = turn["proposal"];
    const std::string tz = turn.value("tz", "UTC");
    const std::string local = turns.local_now(tz).value_or("");
    const std::string token = Tools::access_token(owner).value_or("");
    // A lookup the site's API cannot answer ends the turn with the reason, not a failure.
    Actions::Proposed got = std::string("Could not look that up on the site; try again later.");
    try {
        got = Actions::propose(Net::Http::transport(),
                               s->api_url,
                               token,
                               tz,
                               local.size() >= 10 ? local.substr(0, 10) : "",
                               proposal.value("tool", ""),
                               proposal.value("args", json::object()));
    } catch (const std::exception& e) {
        spdlog::warn("ani agent {}: proposal of turn {} not resolved: {}", Manifest::owner_hash(owner), id, e.what());
    }
    if (const auto* p = std::get_if<Actions::Proposal>(&got)) {
        ActionRepository().add(owner, id, p->kind, p->summary, p->request);
        turns.finish(id, Actions::proposed_text(*p), json::array());
    } else {
        turns.finish(id, std::get<std::string>(got), json::array());
    }
    return json{{"status", "done"}, {"proposal", true}};
}

}  // namespace

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
    if (turn->contains("proposal") && (*turn)["proposal"].is_object()) {
        try {
            return run_proposal(turns, id, owner, *turn);
        } catch (const Jobs::DeferJob&) {
            throw;
        } catch (const std::exception& e) {
            return ended(id, owner, "failed", e.what());
        }
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
        // One turn of a user at a time: the user's own and a webhook turn wait for each other.
        const std::string origin = turn->value("origin", "user");
        reap(owner);
        if (turns.running_other(owner, origin)) {
            throw Jobs::DeferJob("another turn of the user runs", 2000);
        }
        if (!turns.start(id)) {
            const auto again = turns.for_worker(id);
            if (again && again->value("status", "") == "queued") {
                throw Jobs::DeferJob("another turn of the user runs", 2000);
            }
            return json{{"status", "skipped"}, {"reason", "not_queued"}};
        }
        // Read after the start: events may have joined a webhook turn while it was queued.
        const auto started = turns.for_worker(id);
        agents.touch(owner);

        const std::string tz = turn->value("tz", "UTC");
        const std::string local = turns.local_now(tz).value_or("");
        json tools = json::array();
        Gateway::ToolRunner run = [](const std::string&, const json&) { return json{{"error", "unavailable"}}.dump(); };
        if (!s->api_url.empty()) {
            if (const auto token = Tools::access_token(owner)) {
                tools = Tools::definitions();
                run = Tools::runner(Net::Http::transport(), s->api_url, *token, tz);
                // The owner's Telegram, for the one user it serves.
                const bool telegram = TelegramTools::available(owner);
                if (telegram) {
                    for (const auto& t : TelegramTools::definitions()) {
                        tools.push_back(t);
                    }
                    run = TelegramTools::runner(Net::Http::transport(), s->api_url, *token, std::move(run));
                }
                for (const auto& t : FactTools::definitions()) {
                    tools.push_back(t);
                }
                run = FactTools::runner(owner, std::move(run));
                const bool calendar = CalendarTools::available(owner);
                if (calendar) {
                    for (const auto& t : CalendarTools::definitions()) {
                        tools.push_back(t);
                    }
                    run = CalendarTools::runner(Net::Http::transport(), s->api_url, *token, tz, std::move(run));
                }
                // Changes need the person's day (a habit is ticked for today or before).
                if (local.size() >= 10) {
                    for (const auto& t : Actions::definitions()) {
                        tools.push_back(t);
                    }
                    if (telegram) {
                        for (const auto& t : Actions::telegram_definitions()) {
                            tools.push_back(t);
                        }
                    }
                    if (calendar) {
                        for (const auto& t : Actions::calendar_definitions()) {
                            tools.push_back(t);
                        }
                    }
                    run = Actions::runner(
                        Net::Http::transport(), s->api_url, *token, tz, local.substr(0, 10), owner, id, std::move(run));
                }
            }
        }
        const json outcomes = ActionRepository().of_previous_turn(owner, id);
        // The gateway ignores system messages: the context goes in front of the text.
        const std::string said = (started ? *started : *turn).value("text", "");
        // The offset goes with the zone: times she writes for the calendar carry it.
        const std::string zone = tz + (turns.utc_offset(tz) ? " (UTC" + *turns.utc_offset(tz) + ")" : std::string());
        const std::string facts = FactTools::facts_note(FactRepository().list(owner));
        const std::string text = preface(local, zone, tools, outcomes) +
                                 (facts.empty() ? std::string() : "\n" + facts) + "\n" +
                                 (origin == "hook" ? hook_note() + "\n" + said : said);
        const auto out = Gateway::converse(Net::Http::transport(),
                                           "http://" + gateway->first + ":" + std::to_string(Gateway::kGatewayPort),
                                           gateway->second,
                                           session_of(owner, agents.chat_session(owner)),
                                           text,
                                           tools,
                                           run);
        const std::string reply = without_preface(out.reply);
        turns.finish(id, reply, out.tools);
        agents.touch(owner);
        if (origin == "hook") {
            // Her answer is the line the page plays; SILENT, nothing.
            // A long answer is said in part (whole sentences within a line); it is whole in the chat.
            const std::string line = trimmed(reply);
            InboxRepository().resolve_turn(
                id, Hook::is_silent(line) ? std::nullopt : std::optional<std::string>(Now::Voice::spoken_part(line)));
            more_events(owner);
        } else if (origin == "mcp") {
            // An open Now page says the reply aloud, a long one in part; it is whole in the chat.
            const std::string line = Now::Voice::spoken_part(reply);
            if (!line.empty()) {
                InboxRepository().add(
                    owner, "say", json{{"text", line}, {"via", "mcp"}}, std::nullopt, kMcpLineTtl, "ready");
            }
        }
        return json{{"status", "done"}, {"tools", out.tools.size()}};
    } catch (const Jobs::DeferJob&) {
        throw;
    } catch (const Gateway::Error& e) {
        // Woken just now, the agent may still be preparing: the turn waits for it.
        const long long age = turn->value("age_seconds", 0LL);
        const bool again =
            (e.not_ready && age <= kTurnWaitSeconds) || (e.first_call && age <= kFirstRefusalRetrySeconds);
        if (again && turns.requeue(id)) {
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
            InboxRepository().fail_turn(turn_id);
            spdlog::warn("ani agent: turn {} dead-lettered: {}", turn_id, error.substr(0, 200));
        }
    } catch (const std::exception& e) {
        spdlog::warn("ani agent: the dead turn {} was not marked failed: {}", turn_id, e.what());
    }
}

json process_hook_event(const json& payload) {
    const std::string owner = payload.value("owner", std::string());
    if (owner.empty()) {
        throw Jobs::PermanentJobError("ani hook event job without an owner");
    }
    EventLock lock(owner);
    if (!lock.held()) {
        throw Jobs::DeferJob("another event job of the user runs", 1000);
    }
    reap(owner);
    InboxRepository inbox;
    TurnRepository turns;
    const json waiting = inbox.thinking_unlinked(owner);
    json live = json::array();
    for (const auto& item : waiting) {
        if (item.value("expires_in", 0LL) > 0) {
            live.push_back(item);
        }
    }
    if (live.empty()) {
        // Nothing left the agent could still answer in time.
        inbox.fail_unlinked(owner);
        return json{{"status", "skipped"}, {"reason", waiting.empty() ? "no_events" : "expired"}};
    }
    const auto s = read_settings();
    AgentRepository agents;
    if (!s || over_budget(agents, owner, s->monthly_tokens)) {
        inbox.fail_unlinked(owner);
        return json{{"status", "failed"}, {"error", s ? "budget" : "unavailable"}};
    }

    // Events that came while a webhook turn waits join it.
    if (const auto open = turns.open_hook_turn(owner)) {
        const std::string turn_id = (*open)["id"];
        if (open->value("status", "") != "queued") {
            throw Jobs::DeferJob("a webhook turn of the user runs", 2000);
        }
        std::vector<std::string> joined;
        for (const auto& item : live) {
            if (!turns.append_text(turn_id, event_line(item["payload"]))) {
                break;
            }
            joined.push_back(item["id"].get<std::string>());
        }
        inbox.link(joined, turn_id);
        if (joined.size() < live.size()) {
            throw Jobs::DeferJob("the webhook turn is full", 5000);
        }
        return json{{"status", "joined"}, {"events", joined.size()}};
    }

    if (turns.count_since(owner, 3600) >= kTurnsPerHour) {
        throw Jobs::DeferJob("the user's turns for this hour are spent", 60000);
    }
    // No slot to wake the agent: the events wait (until their time is out, above).
    if (wake(owner) == Wake::Busy) {
        throw Jobs::DeferJob("every agent slot is taken", 30000);
    }
    std::string text;
    std::vector<std::string> ids;
    for (const auto& item : live) {
        const std::string line = event_line(item["payload"]);
        if (!text.empty() && utf8_length(text) + 1 + utf8_length(line) > kTurnTextMax) {
            break;
        }
        text += (text.empty() ? "" : "\n") + line;
        ids.push_back(item["id"].get<std::string>());
    }
    const auto turn = turns.open(owner, text, turns.last_tz(owner), "hook");
    if (!turn) {
        throw Jobs::DeferJob("a webhook turn opened meanwhile", 1000);
    }
    const std::string turn_id = (*turn)["id"];
    inbox.link(ids, turn_id);
    Jobs::get().submit(kTurnJob, json{{"turn", turn_id}});
    if (ids.size() < live.size()) {
        Jobs::get().submit(Hook::kEventJob, json{{"owner", owner}});
    }
    return json{{"status", "queued"}, {"turn", turn_id}, {"events", ids.size()}};
}

void fail_hook_events(const std::string& owner, const std::string& error) {
    try {
        if (!owner.empty()) {
            InboxRepository().fail_unlinked(owner);
            spdlog::warn(
                "ani agent {}: event job dead-lettered: {}", Manifest::owner_hash(owner), error.substr(0, 200));
        }
    } catch (const std::exception& e) {
        spdlog::warn("ani agent: the waiting events were not failed: {}", e.what());
    }
}

json process_inbox_purge(const json& /*payload*/) {
    // Asks that ran out unanswered tell their senders before they are purged.
    const int callbacks = Intake::send_callbacks();
    return json{{"status", "done"}, {"rows", InboxRepository().purge()}, {"callbacks", callbacks}};
}

}  // namespace Agent
