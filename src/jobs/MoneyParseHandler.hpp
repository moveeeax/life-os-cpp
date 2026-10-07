/**
 * @file MoneyParseHandler.hpp
 * @brief money_parse job handler: a bank mail, a list or a receipt photo to
 *        the LLM, the checked lines into the job row. Nothing reaches the
 *        ledger here: the user accepts the lines into the inbox afterwards.
 *
 * Payload: {job_id, owner_id, max_attempts}. The request carries the user's
 * accounts (with the card's last four digits), categories and the merchants
 * the ledger remembers, so the model can match instead of guess.
 *
 * Outcomes as in the food parse: a refusal, a bad answer, a missing
 * configuration or a disabled module close the job as failed; an outage
 * requeues it until the queue's last attempt, which closes it as failed.
 */

#pragma once

#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "core/Modules.hpp"
#include "jobs/Jobs.hpp"
#include "llm/Chat.hpp"
#include "money/Llm.hpp"
#include "money/ParseAnswer.hpp"
#include "net/Http.hpp"
#include "repositories/money/AccountRepository.hpp"
#include "repositories/money/CategoryRepository.hpp"
#include "repositories/money/MerchantRepository.hpp"
#include "repositories/money/ParseJobRepository.hpp"

namespace Jobs::MoneyParse {

inline constexpr const char* kJobType = "money_parse";
/// How many remembered merchants travel with the request, most used first.
inline constexpr long kMerchantsInPrompt = 300;

inline nlohmann::json process_job(const nlohmann::json& payload) {
    if (!payload.contains("job_id") || !payload["job_id"].is_string() || payload["job_id"].get<std::string>().empty()) {
        throw std::invalid_argument("money_parse: payload has no job_id");
    }
    const std::string job_id = payload["job_id"].get<std::string>();
    Repositories::Money::ParseJobRepository jobs;
    const auto failed = [&](const std::string& code, const std::string& message) {
        jobs.fail(job_id, code, message);
        spdlog::warn("money parse {}: {}", job_id, code);
        return nlohmann::json{{"job_id", job_id}, {"status", "failed"}, {"error", code}};
    };

    if (!Core::money_enabled()) {
        return failed("money_disabled", "the money module is off on this worker");
    }
    const auto settings = Money::Llm::parse_settings();
    if (!settings.has_value()) {
        return failed("not_configured", "MONEY_LLM_* settings are missing");
    }
    const auto job = jobs.load_for_worker(job_id);
    if (!job.has_value()) {
        return {{"job_id", job_id}, {"status", "missing"}};
    }
    const int attempt = jobs.start(job_id);
    if (attempt == 0) {
        return {{"job_id", job_id}, {"status", job->value("status", "unknown")}};
    }
    const std::string owner = job->value("owner_id", std::string());
    const int max_attempts =
        payload.contains("max_attempts") && payload["max_attempts"].is_number_integer()
            ? payload["max_attempts"].get<int>()
            : (Jobs::is_initialized() ? Jobs::get().default_max_retries() : Jobs::kDefaultMaxRetries);
    const auto give_up_or_retry = [&](const char* code, const std::string& what) -> nlohmann::json {
        if (attempt >= max_attempts) {
            return failed(code, what);
        }
        jobs.requeue(job_id);
        throw std::runtime_error(std::string("money_parse: ") + what);
    };

    try {
        std::set<std::string> own_accounts;
        std::map<std::string, std::string> own_categories;
        nlohmann::json accounts = nlohmann::json::array();
        for (const auto& a : Repositories::Money::AccountRepository().list(owner, false)) {
            own_accounts.insert(a["id"].get<std::string>());
            accounts.push_back({{"id", a["id"]},
                                {"name", a["name"]},
                                {"bank", a["bank"]},
                                {"last4", a["last4"]},
                                {"currency", a["currency"]}});
        }
        nlohmann::json categories = nlohmann::json::array();
        for (const auto& c : Repositories::Money::CategoryRepository().list(owner, false)) {
            own_categories[c["id"].get<std::string>()] = c["kind"].get<std::string>();
            categories.push_back({{"id", c["id"]}, {"name", c["name"]}, {"kind", c["kind"]}});
        }
        nlohmann::json merchants = nlohmann::json::array();
        for (const auto& m : Repositories::Money::MerchantRepository().top(owner, kMerchantsInPrompt)) {
            merchants.push_back(
                {{"merchant", m["display_name"]}, {"category_id", m["category_id"]}, {"times", m["times"]}});
        }

        const std::string hint_date = job->value("hint_date", std::string());
        const bool receipt = job->value("kind", std::string()) == "receipt";
        nlohmann::json user_message{
            {"hint_date", hint_date}, {"accounts", accounts}, {"categories", categories}, {"merchants", merchants}};
        nlohmann::json user_content;
        std::string model = settings->model;
        if (receipt) {
            const std::string image = (*job)["image"].is_string() ? (*job)["image"].get<std::string>() : "";
            if (image.empty()) {
                return failed("invalid_job", "the receipt has no photo");
            }
            user_content = nlohmann::json::array(
                {{{"type", "text"}, {"text", user_message.dump()}},
                 {{"type", "image_url"},
                  {"image_url",
                   {{"url", "data:" + job->value("image_type", std::string("image/jpeg")) + ";base64," + image}}}}});
            model = Money::Llm::vision_model(*settings);
        } else {
            user_message["text"] = job->value("text", std::string());
            user_content = user_message.dump();
        }
        const nlohmann::json request{
            {"model", model},
            {"messages",
             {{{"role", "system"}, {"content", settings->prompt}}, {{"role", "user"}, {"content", user_content}}}},
            {"max_tokens", settings->max_tokens},
            {"temperature", 0},
            {"response_format", {{"type", "json_object"}}}};

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
        const std::string content = Chat::content_of(body);
        if (content.empty()) {
            return failed("invalid_answer", "no message content");
        }
        std::vector<Money::Parse::Line> lines;
        try {
            lines = Money::Parse::parse_answer(content, hint_date, own_accounts, own_categories);
        } catch (const Money::Parse::Invalid& e) {
            return failed("invalid_answer", e.what());
        }
        jobs.finish(job_id,
                    Money::Parse::to_json(lines),
                    body.contains("model") && body["model"].is_string() ? body["model"].get<std::string>() : model,
                    Chat::usage(body, "prompt_tokens"),
                    Chat::usage(body, "completion_tokens"));
        return {{"job_id", job_id}, {"status", "done"}, {"lines", lines.size()}};
    } catch (const std::runtime_error& e) {
        // give_up_or_retry's own throw: the queue retries.
        if (std::string(e.what()).rfind("money_parse: ", 0) == 0) {
            throw;
        }
        return give_up_or_retry("internal_error", e.what());
    } catch (const std::exception& e) {
        return give_up_or_retry("internal_error", e.what());
    }
}

}  // namespace Jobs::MoneyParse
