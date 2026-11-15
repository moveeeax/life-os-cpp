/**
 * @file HookEvents.cpp
 * @brief The webhook event job and the inbox purge of src/agent/Turn.hpp:
 *        the user's thinking events join their queued webhook turn or open one.
 */

#include <chrono>
#include <exception>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "agent/Intake.hpp"
#include "agent/Lifecycle.hpp"
#include "agent/Manifest.hpp"
#include "agent/Settings.hpp"
#include "agent/Turn.hpp"
#include "agent/TurnShared.hpp"
#include "cache/Lock.hpp"
#include "jobs/Job.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/agent/AgentRepository.hpp"
#include "repositories/agent/InboxRepository.hpp"
#include "repositories/agent/TurnRepository.hpp"
#include "utils/Utf8.hpp"

namespace Agent {

namespace {

using json = nlohmann::json;
using detail::over_budget;
using detail::reap;
using Repositories::Agent::AgentRepository;
using Repositories::Agent::InboxRepository;
using Repositories::Agent::TurnRepository;

/// One event job of a user at a time (Redis), so two never take the same events.
class EventLock : public Cache::Lock {
public:
    explicit EventLock(const std::string& owner)
        : Cache::Lock("ani:hook:event:lock:" + owner, std::chrono::seconds(60)) {}
};

/// One pass of the user's event job; a DeferJob brings the same job back.
json hook_event_pass(const std::string& owner) {
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
            // The only event job of the user waits for the turn (new events do not queue another).
            throw Jobs::DeferJob("a webhook turn of the user runs", 5000);
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

    // Webhook turns have their own hourly cap: a busy Telegram never takes the person's chat.
    if (turns.count_since(owner, 3600, /*hooks=*/true) >= kHookTurnsPerHour) {
        throw Jobs::DeferJob("the user's webhook turns for this hour are spent", 60000);
    }
    // No slot to wake the agent: the events wait (until their time is out, above).
    if (wake(owner) == Wake::Busy) {
        throw Jobs::DeferJob("every agent slot is taken", 30000);
    }
    std::string text;
    std::vector<std::string> ids;
    for (const auto& item : live) {
        const std::string line = event_line(item["payload"]);
        if (!text.empty() && Utils::Utf8::length(text) + 1 + Utils::Utf8::length(line) > kTurnTextMax) {
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
    // Events that did not fit wait unlinked: event_job_ended queues the next pass for them.
    return json{{"status", "queued"}, {"turn", turn_id}, {"events", ids.size()}};
}

}  // namespace

json process_hook_event(const json& payload) {
    const std::string owner = payload.value("owner", std::string());
    if (owner.empty()) {
        throw Jobs::PermanentJobError("ani hook event job without an owner");
    }
    Intake::event_job_alive(owner);
    // A deferral or an error keeps the marker: this job comes back (an error is retried, a
    // dead letter clears the marker in fail_hook_events).
    json out = hook_event_pass(owner);
    Intake::event_job_ended(owner);
    return out;
}

void fail_hook_events(const std::string& owner, const std::string& error) {
    try {
        if (!owner.empty()) {
            InboxRepository().fail_unlinked(owner);
            spdlog::warn(
                "ani agent {}: event job dead-lettered: {}", Manifest::owner_hash(owner), error.substr(0, 200));
            Intake::event_job_ended(owner);
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
