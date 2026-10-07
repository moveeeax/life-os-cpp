/**
 * @file MoneyAdvisorHandler.hpp
 * @brief money_advisor job: the facts of a period (the Reports page's numbers,
 *        category names instead of ids, balances, the person's note) to the
 *        LLM, the written review and the facts into the report row.
 *
 * Payload: {report_id, owner_id, max_attempts}. A period without posted rows
 * gets a one-line review and no provider call. Provider rules, attempts and
 * the stale claim as the money parse.
 */

#pragma once

#include <chrono>
#include <map>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "core/Modules.hpp"
#include "jobs/Jobs.hpp"
#include "llm/Chat.hpp"
#include "money/Llm.hpp"
#include "money/Period.hpp"
#include "net/Http.hpp"
#include "repositories/money/AccountRepository.hpp"
#include "repositories/money/AdvisorReportRepository.hpp"
#include "repositories/money/CategoryRepository.hpp"
#include "repositories/money/ReportBuilder.hpp"
#include "repositories/money/SettingsRepository.hpp"
#include "utils/Utf8.hpp"

namespace Jobs::MoneyAdvisor {

inline constexpr const char* kJobType = "money_advisor";
inline constexpr const char* kNothing = "Nothing to review: no posted rows in this period.";

/// The facts the model gets for one owner and period.
inline nlohmann::json facts_for(const std::string& owner,
                                const std::string& period,
                                const std::string& from,
                                const std::string& to,
                                const std::string& today) {
    using Money::Period::Kind;
    const Kind kind = period == "week" ? Kind::week : period == "quarter" ? Kind::quarter : Kind::month;
    const auto settings = Repositories::Money::SettingsRepository().load(owner);
    const std::string target =
        settings["view_currency"].is_string() ? settings["view_currency"].get<std::string>() : "";
    nlohmann::json facts =
        Repositories::Money::ReportBuilder::build(owner, kind, period, Money::Period::Range{from, to}, today, target);

    std::map<std::string, std::string> names;
    for (const auto& c : Repositories::Money::CategoryRepository().list(owner, true)) {
        names[c["id"].get<std::string>()] = c["name"].get<std::string>();
    }
    for (auto& block : facts["blocks"]) {
        for (auto& c : block["categories"]) {
            const std::string id = c["category_id"].get<std::string>();
            c["category"] = names.count(id) ? names[id] : "Unknown";
            c.erase("category_id");
        }
    }
    std::map<std::string, double> totals;
    for (const auto& a : Repositories::Money::AccountRepository().list(owner, false)) {
        totals[a["currency"].get<std::string>()] += a["balance"].get<double>();
    }
    nlohmann::json balances = nlohmann::json::array();
    for (const auto& [currency, total] : totals) {
        balances.push_back({{"currency", currency}, {"total", total}});
    }
    facts["balances"] = balances;
    facts["partial_period"] = today <= to;
    const std::string note = settings.value("advisor_note", std::string());
    if (!note.empty()) {
        facts["note"] = note;
    }
    return facts;
}

inline nlohmann::json process_job(const nlohmann::json& payload) {
    if (!payload.contains("report_id") || !payload["report_id"].is_string() ||
        payload["report_id"].get<std::string>().empty()) {
        throw std::invalid_argument("money_advisor: payload has no report_id");
    }
    const std::string id = payload["report_id"].get<std::string>();
    Repositories::Money::AdvisorReportRepository reports;
    const auto failed = [&](const std::string& code, const std::string& message) {
        reports.fail(id, code, message);
        spdlog::warn("money advisor {}: {}", id, code);
        return nlohmann::json{{"report_id", id}, {"status", "failed"}, {"error", code}};
    };

    if (!Core::money_enabled()) {
        return failed("money_disabled", "the money module is off on this worker");
    }
    const auto settings = Money::Llm::advisor_settings();
    if (!settings.has_value()) {
        return failed("not_configured", "MONEY_LLM_* advisor settings are missing");
    }
    const auto row = reports.load_for_worker(id);
    if (!row.has_value()) {
        return {{"report_id", id}, {"status", "missing"}};
    }
    const int attempt = reports.start(id);
    if (attempt == 0) {
        return {{"report_id", id}, {"status", row->value("status", "unknown")}};
    }
    const int max_attempts =
        payload.contains("max_attempts") && payload["max_attempts"].is_number_integer()
            ? payload["max_attempts"].get<int>()
            : (Jobs::is_initialized() ? Jobs::get().default_max_retries() : Jobs::kDefaultMaxRetries);
    const auto give_up_or_retry = [&](const char* code, const std::string& what) -> nlohmann::json {
        if (attempt >= max_attempts) {
            return failed(code, what);
        }
        reports.requeue(id);
        throw std::runtime_error(std::string("money_advisor: ") + what);
    };

    try {
        const std::string owner = row->value("owner_id", std::string());
        const nlohmann::json facts =
            facts_for(owner,
                      row->value("period", std::string("week")),
                      row->value("period_start", std::string()),
                      row->value("period_end", std::string()),
                      payload.value("today",
                                    Money::Period::detail::text_of(
                                        std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()))));
        if (facts["blocks"].empty()) {
            reports.finish(id, facts, kNothing, "", std::nullopt, std::nullopt);
            return {{"report_id", id}, {"status", "done"}, {"empty", true}};
        }
        const nlohmann::json request{
            {"model", settings->model},
            {"messages",
             {{{"role", "system"}, {"content", settings->prompt}}, {{"role", "user"}, {"content", facts.dump()}}}},
            {"max_tokens", settings->max_tokens},
            {"temperature", 0.3}};
        nlohmann::json body;
        std::string error_code;
        try {
            body = Chat::complete_adapting(Net::Http::transport(), *settings, request, error_code);
        } catch (const Chat::Retryable& e) {
            return give_up_or_retry("provider_unavailable", e.what());
        }
        if (!error_code.empty()) {
            return failed(error_code, Chat::provider_message(body));
        }
        const std::string content = Utils::Utf8::cut(Chat::content_of(body), 19000);
        if (content.empty()) {
            return failed("invalid_answer", "no message content");
        }
        reports.finish(id,
                       facts,
                       content,
                       Chat::model_of(body, *settings),
                       Chat::usage(body, "prompt_tokens"),
                       Chat::usage(body, "completion_tokens"));
        return {{"report_id", id}, {"status", "done"}};
    } catch (const std::runtime_error& e) {
        if (std::string(e.what()).rfind("money_advisor: ", 0) == 0) {
            throw;
        }
        return give_up_or_retry("internal_error", e.what());
    } catch (const std::exception& e) {
        return give_up_or_retry("internal_error", e.what());
    }
}

/**
 * Queue the week that just ended for every owner whose advisor runs on this
 * weekday (1 = Monday). The unique key makes a second call a no-op.
 * @returns how many reviews were queued.
 */
inline int enqueue_weekly(int weekday, const std::string& today) {
    const auto last_week =
        Money::Period::previous(Money::Period::Kind::week, Money::Period::of(Money::Period::Kind::week, today));
    Repositories::Money::AdvisorReportRepository reports;
    int n = 0;
    for (const auto& owner : Repositories::Money::SettingsRepository().advisor_owners(weekday)) {
        const auto [row, queued] = reports.create_or_get(owner, "week", last_week.from, last_week.to);
        if (!queued) {
            continue;
        }
        Jobs::get().submit(
            kJobType,
            nlohmann::json{
                {"report_id", row["id"]}, {"owner_id", owner}, {"max_attempts", Jobs::get().default_max_retries()}});
        ++n;
    }
    return n;
}

}  // namespace Jobs::MoneyAdvisor
