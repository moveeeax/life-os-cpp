/**
 * @file Intake.cpp
 * @brief Body for src/agent/Intake.hpp.
 */

#include "agent/Intake.hpp"

#include <exception>

#include <spdlog/spdlog.h>

#include "agent/Settings.hpp"
#include "cache/Cache.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/agent/AgentRepository.hpp"
#include "repositories/agent/HookRepository.hpp"
#include "repositories/agent/InboxRepository.hpp"
#include "repositories/agent/PrefsRepository.hpp"
#include "webhooks/Webhooks.hpp"

namespace Agent::Intake {

namespace {

using json = nlohmann::json;

/// Why her agent cannot take an event now; nullopt when it can.
std::optional<Refused> agent_refusal(const std::string& owner) {
    const auto s = read_settings();
    if (!s) {
        return Refused{409, "agent_off", "her agent is off: send say, outfit or act"};
    }
    const json used = Repositories::Agent::AgentRepository().month_usage(owner);
    if (used.value("prompt_tokens", 0LL) + used.value("completion_tokens", 0LL) >= s->monthly_tokens) {
        return Refused{429, "agent_budget", "her agent's tokens for this month are spent"};
    }
    return std::nullopt;
}

}  // namespace

bool within_rate(const std::string& owner) {
    if (!Cache::is_initialized()) {
        return true;
    }
    try {
        const std::string key = "ani:hook:rate:" + owner;
        const long long n = Cache::get().incr(key);
        if (n == 1) {
            Cache::get().expire(key, Hook::kWindowSeconds);
        }
        return n <= Hook::kCallsPerHour;
    } catch (const std::exception& e) {
        spdlog::warn("ani.intake: the rate counter failed: {}", e.what());
        return true;
    }
}

std::variant<Accepted, Refused> accept(const std::string& owner, Hook::Item item, const std::string& via) {
    if (item.kind == "outfit" && item.payload.value("outfit", "") == "bare" &&
        !Repositories::Agent::PrefsRepository().get(owner).value("bare", false)) {
        return Refused{422, "bare_off", "the bare outfit is off in Ani's settings"};
    }
    const bool event = item.kind == "event";
    if (event) {
        if (auto refused = agent_refusal(owner)) {
            return *refused;
        }
        if (!Jobs::is_initialized()) {
            return Refused{503, "jobs_disabled", "Job queue not enabled"};
        }
    }
    if (item.callback && Webhooks::detail::host_is_blocked(Webhooks::detail::url_host(*item.callback))) {
        return Refused{400, "bad_callback", "callback must be a public https address"};
    }
    Repositories::Agent::InboxRepository inbox;
    const bool hold = item.kind == "hold";
    if (!hold && inbox.pending_count(owner) >= Hook::kPendingMax) {
        return Refused{429, "backlog", "50 items wait for the page already"};
    }
    item.payload["via"] = via;
    const std::string status = hold ? "delivered" : event ? "thinking" : "ready";
    const auto added = inbox.add(
        owner, item.kind, item.payload, item.source_id, item.ttl, status, {item.delay_seconds, item.at, item.callback});
    if (added.bad_time) {
        return Refused{400, "invalid_when", "a reminder is due between now and a week ahead"};
    }
    if (added.duplicate) {
        return Refused{409, "duplicate", "this id was sent in the last 24 hours", json{{"id", added.id}}};
    }
    if (event) {
        Jobs::get().submit(Hook::kEventJob, json{{"owner", owner}});
    }
    if (hold) {
        Repositories::Agent::PrefsRepository().hold(owner, item.payload.value("minutes", 0));
    }
    if (via == "webhook") {
        Repositories::Agent::HookRepository().touch(owner);
    }
    return Accepted{added.id};
}

Repositories::Agent::InboxRepository::Answer answer(const std::string& owner,
                                                    const std::string& id,
                                                    const std::string& text) {
    const auto a = Repositories::Agent::InboxRepository().answer(owner, id, text);
    if (a == Repositories::Agent::InboxRepository::Answer::Ok) {
        // The answer is kept whatever happens here; the hourly purge retries.
        try {
            send_callbacks();
        } catch (const std::exception& e) {
            spdlog::warn("ani.intake: queueing answer callbacks failed: {}", e.what());
        }
    }
    return a;
}

int send_callbacks() {
    if (!Jobs::is_initialized()) {
        return 0;
    }
    int sent = 0;
    for (const auto& c : Repositories::Agent::InboxRepository().due_callbacks()) {
        json body{{"id", c.id},
                  {"source_id", c.source_id ? json(*c.source_id) : json(nullptr)},
                  {"question", c.question},
                  {"answer", c.answer ? json(*c.answer) : json(nullptr)}};
        if (!c.answer) {
            body["reason"] = "expired";
        }
        // Signed with the user's webhook token, which the sender already holds.
        try {
            const std::string secret = Repositories::Agent::HookRepository().ensure(c.owner);
            Jobs::get().submit(Webhooks::kJobType,
                               json{{"url", c.url}, {"event", "ani.answer"}, {"body", body}, {"secret", secret}});
            ++sent;
        } catch (const std::exception& e) {
            spdlog::warn("ani.intake: callback {} not queued, kept for the next try: {}", c.id, e.what());
            Repositories::Agent::InboxRepository().unsend(c.id);
        }
    }
    return sent;
}

}  // namespace Agent::Intake
