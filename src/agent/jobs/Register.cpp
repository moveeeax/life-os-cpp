/**
 * @file Register.cpp
 * @brief Body for src/agent/jobs/Register.hpp — compiled once into app_core.
 */

#include "agent/jobs/Register.hpp"

#include <string>

#include <nlohmann/json.hpp>

#include "agent/Actions.hpp"
#include "agent/Hook.hpp"
#include "agent/Lifecycle.hpp"
#include "agent/Reminders.hpp"
#include "agent/TelegramPoll.hpp"
#include "agent/Turn.hpp"
#include "jobs/Dispatcher.hpp"
#include "jobs/Tick.hpp"

namespace Agent {

void register_jobs(Jobs::Dispatcher& d) {
    d.register_handler(kStartJob, [](const nlohmann::json& payload) { return process_start(payload); });
    d.register_handler(kStopJob, [](const nlohmann::json& payload) { return process_stop(payload); });
    // The minute's ticks (core/Core.cpp, jobs/Tick.hpp): a job that ends lets the next tick queue one.
    d.register_handler(kReconcileJob, [](const nlohmann::json& payload) {
        auto out = process_reconcile(payload);
        Jobs::Tick::done(kReconcileJob);
        return out;
    });
    d.register_handler(kTurnJob, [](const nlohmann::json& payload) { return process_turn(payload); });
    d.register_handler(Hook::kEventJob, [](const nlohmann::json& payload) { return process_hook_event(payload); });
    d.register_handler(kInboxPurgeJob, [](const nlohmann::json& payload) { return process_inbox_purge(payload); });
    d.register_handler(TelegramPoll::kPollJob, [](const nlohmann::json& payload) {
        auto out = TelegramPoll::process_poll(payload);
        Jobs::Tick::done(TelegramPoll::kPollJob);
        return out;
    });
    d.register_handler(Reminders::kDueJob, [](const nlohmann::json& payload) {
        auto out = Reminders::process_due(payload);
        Jobs::Tick::done(Reminders::kDueJob);
        return out;
    });
    d.register_handler(Actions::kActionJob,
                       [](const nlohmann::json& payload) { return Actions::process_action(payload); });
    // A start that dead-letters leaves its row starting; stop it, so the next wake can try again.
    d.register_dead_letter(kStartJob, [](const nlohmann::json& payload, const std::string& error) {
        request_stop(payload.value("owner", std::string()), "the start failed: " + error.substr(0, 200));
    });
    // A turn whose job dead-letters would keep the chat waiting until it goes stale.
    d.register_dead_letter(kTurnJob, [](const nlohmann::json& payload, const std::string& error) {
        fail_turn(payload.value("turn", std::string()), error);
    });
    // Webhook events whose job dead-letters would wait as thinking until they expire.
    d.register_dead_letter(Hook::kEventJob, [](const nlohmann::json& payload, const std::string& error) {
        fail_hook_events(payload.value("owner", std::string()), error);
    });
    // So does a confirmed action, which would show as being done for ever.
    d.register_dead_letter(Actions::kActionJob, [](const nlohmann::json& payload, const std::string& error) {
        Actions::fail_action(payload.value("action", std::string()), error);
    });
}

}  // namespace Agent
