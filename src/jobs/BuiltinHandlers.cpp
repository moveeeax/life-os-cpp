/**
 * @file BuiltinHandlers.cpp
 * @brief Body for src/jobs/BuiltinHandlers.hpp — compiled once into app_core.
 *        The only jobs TU that sees the email/webhooks handlers it wires up.
 */

#include "jobs/BuiltinHandlers.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

#include <nlohmann/json.hpp>

#include "email/AccountEmailWorker.hpp"
#include "email/GenericEmail.hpp"
#include "jobs/Dispatcher.hpp"
#include "jobs/FitnessSyncHandler.hpp"
#include "jobs/FoodParseHandler.hpp"
#include "jobs/MoneyAdvisorHandler.hpp"
#include "jobs/MoneyParseHandler.hpp"
#include "jobs/MoneyRatesHandler.hpp"
#include "jobs/TasksParseHandler.hpp"
#include "webhooks/Webhooks.hpp"

namespace Jobs {

void register_builtin_handlers() {
    auto& d = Dispatcher::get();
    // Account emails (confirm / reset / change-email / invite). Throws on
    // render/SMTP failure → retried, then DLQ'd.
    d.register_handler(Email::AccountEmails::kJobType,
                       [](const json& payload) { return Email::AccountEmails::process_job(payload); });
    // Generic ad-hoc email for any app code (not tied to account flows). Same
    // throw-on-failure → retry/DLQ contract.
    d.register_handler(Email::SendEmail::kJobType,
                       [](const json& payload) { return Email::SendEmail::process_job(payload); });
    // Outbound webhooks: signed POST to a subscriber URL, same retry/DLQ contract.
    d.register_handler(Webhooks::kJobType, [](const json& payload) { return Webhooks::process_job(payload); });
    d.register_handler(FitnessSync::kJobType, [](const json& payload) { return FitnessSync::process_job(payload); });
    // Food: a text description to the LLM; a bad answer closes the job, a
    // provider outage throws for a retry.
    d.register_handler(FoodParse::kJobType, [](const json& payload) { return FoodParse::process_job(payload); });
    d.register_handler(MoneyRates::kJobType, [](const json& payload) { return MoneyRates::process_job(payload); });
    d.register_handler(MoneyParse::kJobType, [](const json& payload) { return MoneyParse::process_job(payload); });
    d.register_handler(MoneyAdvisor::kJobType, [](const json& payload) { return MoneyAdvisor::process_job(payload); });
    d.register_handler(TasksParse::kJobType, [](const json& payload) { return TasksParse::process_job(payload); });
    // When the queue gives one of the LLM jobs up, its row is closed as failed
    // (if still queued or running) instead of waiting for a page that polls it.
    const auto row_of = [](const json& payload, const char* key) {
        return payload.contains(key) && payload[key].is_string() ? payload[key].get<std::string>() : std::string();
    };
    d.register_dead_letter(FoodParse::kJobType, [row_of](const json& payload, const std::string& error) {
        if (const auto id = row_of(payload, "job_id"); !id.empty())
            Repositories::ParseJobRepository().fail(id, "queue_gave_up", error);
    });
    d.register_dead_letter(MoneyParse::kJobType, [row_of](const json& payload, const std::string& error) {
        if (const auto id = row_of(payload, "job_id"); !id.empty())
            Repositories::Money::ParseJobRepository().fail(id, "queue_gave_up", error);
    });
    d.register_dead_letter(MoneyAdvisor::kJobType, [row_of](const json& payload, const std::string& error) {
        if (const auto id = row_of(payload, "report_id"); !id.empty())
            Repositories::Money::AdvisorReportRepository().fail(id, "queue_gave_up", error);
    });
    d.register_dead_letter(TasksParse::kJobType, [row_of](const json& payload, const std::string& error) {
        if (const auto id = row_of(payload, "job_id"); !id.empty())
            Repositories::Tasks::ParseJobRepository().fail(id, "queue_gave_up", error);
    });
    // Demo handlers used by examples/tests.
    d.register_handler("echo", [](const json& payload) { return payload; });
    d.register_handler("slow", [](const json& payload) -> json {
        int seconds = 2;
        if (payload.contains("seconds") && payload["seconds"].is_number())
            seconds = std::max(1, std::min(payload["seconds"].get<int>(), 30));
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        return {{"slept_seconds", seconds}, {"message", "done"}};
    });
    d.register_handler("fail",
                       [](const json&) -> json { throw std::runtime_error("intentional failure for testing"); });
}

}  // namespace Jobs
