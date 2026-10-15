/**
 * @file Register.cpp
 * @brief Body for src/money/jobs/Register.hpp — compiled once into app_core.
 */

#include "money/jobs/Register.hpp"

#include <string>

#include <nlohmann/json.hpp>

#include "jobs/Dispatcher.hpp"
#include "money/jobs/AdvisorJob.hpp"
#include "money/jobs/ParseJob.hpp"
#include "money/jobs/RatesJob.hpp"
#include "repositories/money/AdvisorReportRepository.hpp"
#include "repositories/money/ParseJobRepository.hpp"

namespace Money {

void register_jobs(Jobs::Dispatcher& d) {
    using nlohmann::json;
    d.register_handler(Jobs::MoneyRates::kJobType,
                       [](const json& payload) { return Jobs::MoneyRates::process_job(payload); });
    d.register_handler(Jobs::MoneyParse::kJobType,
                       [](const json& payload) { return Jobs::MoneyParse::process_job(payload); });
    d.register_handler(Jobs::MoneyAdvisor::kJobType,
                       [](const json& payload) { return Jobs::MoneyAdvisor::process_job(payload); });
    // When the queue gives one of the LLM jobs up, its row is closed as failed
    // (if still queued or running) instead of waiting for a page that polls it.
    const auto row_of = [](const json& payload, const char* key) {
        return payload.contains(key) && payload[key].is_string() ? payload[key].get<std::string>() : std::string();
    };
    d.register_dead_letter(Jobs::MoneyParse::kJobType, [row_of](const json& payload, const std::string& error) {
        if (const auto id = row_of(payload, "job_id"); !id.empty())
            Repositories::Money::ParseJobRepository().fail(id, "queue_gave_up", error);
    });
    d.register_dead_letter(Jobs::MoneyAdvisor::kJobType, [row_of](const json& payload, const std::string& error) {
        if (const auto id = row_of(payload, "report_id"); !id.empty())
            Repositories::Money::AdvisorReportRepository().fail(id, "queue_gave_up", error);
    });
}

}  // namespace Money
