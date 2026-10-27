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
    Repositories::Agent::InboxRepository inbox;
    if (inbox.pending_count(owner) >= Hook::kPendingMax) {
        return Refused{429, "backlog", "50 items wait for the page already"};
    }
    item.payload["via"] = via;
    const auto added =
        inbox.add(owner, item.kind, item.payload, item.source_id, item.ttl, event ? "thinking" : "ready");
    if (added.duplicate) {
        return Refused{409, "duplicate", "this id was sent in the last 24 hours", json{{"id", added.id}}};
    }
    if (event) {
        Jobs::get().submit(Hook::kEventJob, json{{"owner", owner}});
    }
    if (via == "webhook") {
        Repositories::Agent::HookRepository().touch(owner);
    }
    return Accepted{added.id};
}

}  // namespace Agent::Intake
