/**
 * @file ParseJob.hpp
 * @brief money_parse job handler: a bank mail, a list or a receipt photo to
 *        the LLM, the checked lines into the job row. The user accepts the
 *        lines into the inbox afterwards, unless the text job was created
 *        with apply: then the worker posts them itself (money/jobs/ParseApply.hpp).
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
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "jobs/Jobs.hpp"
#include "llm/Chat.hpp"
#include "money/Llm.hpp"
#include "money/ParseAnswer.hpp"
#include "money/jobs/ParseApply.hpp"
#include "net/Http.hpp"
#include "repositories/money/AccountRepository.hpp"
#include "repositories/money/CategoryRepository.hpp"
#include "repositories/money/MerchantRepository.hpp"
#include "repositories/money/ParseJobRepository.hpp"
#include "utils/Modules.hpp"

namespace Jobs::MoneyParse {

inline constexpr const char* kJobType = "money_parse";
/// How many remembered merchants travel with the request, most used first.
inline constexpr long kMerchantsInPrompt = 300;

/// What the parse request carries about the user: the accounts (with the
/// card's last four digits), the categories and the merchants the ledger
/// remembers, and the ids an answer may name. The unified parse sends the same.
struct PromptContext {
    std::set<std::string> own_accounts;
    std::map<std::string, std::string> own_categories;
    nlohmann::json accounts = nlohmann::json::array();
    nlohmann::json categories = nlohmann::json::array();
    nlohmann::json merchants = nlohmann::json::array();

    nlohmann::json message(const std::string& hint_date) const {
        return {{"hint_date", hint_date}, {"accounts", accounts}, {"categories", categories}, {"merchants", merchants}};
    }
};

inline PromptContext prompt_context(const std::string& owner) {
    PromptContext ctx;
    for (const auto& a : Repositories::Money::AccountRepository().list(owner, false)) {
        ctx.own_accounts.insert(a["id"].get<std::string>());
        ctx.accounts.push_back({{"id", a["id"]},
                                {"name", a["name"]},
                                {"bank", a["bank"]},
                                {"last4", a["last4"]},
                                {"currency", a["currency"]}});
    }
    for (const auto& c : Repositories::Money::CategoryRepository().list(owner, false)) {
        ctx.own_categories[c["id"].get<std::string>()] = c["kind"].get<std::string>();
        ctx.categories.push_back({{"id", c["id"]}, {"name", c["name"]}, {"kind", c["kind"]}});
    }
    for (const auto& m : Repositories::Money::MerchantRepository().top(owner, kMerchantsInPrompt)) {
        ctx.merchants.push_back(
            {{"merchant", m["display_name"]}, {"category_id", m["category_id"]}, {"times", m["times"]}});
    }
    return ctx;
}

inline nlohmann::json process_job(const nlohmann::json& payload) {
    if (!payload.contains("job_id") || !payload["job_id"].is_string() || payload["job_id"].get<std::string>().empty()) {
        throw std::invalid_argument("money_parse: payload has no job_id");
    }
    const std::string job_id = payload["job_id"].get<std::string>();
    Repositories::Money::ParseJobRepository jobs;
    int attempt = 0;  // 0 until the claim: a fail before it is not fenced by an attempt
    const auto failed = [&](const std::string& code, const std::string& message) {
        jobs.fail(job_id, code, message, attempt);
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
    attempt = jobs.start(job_id);
    if (attempt == 0) {
        // A row still `running` is held by another delivery, or by a worker that died less than
        // JobRows::kStaleRunning ago: the job waits until the row turns stale, without spending an attempt.
        const auto now = jobs.status(job_id);
        if (now == "running") {
            throw Jobs::DeferJob("money_parse: the row is running elsewhere", jobs.stale_in_ms(job_id));
        }
        return {{"job_id", job_id}, {"status", now.value_or("missing")}};
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
        jobs.requeue(job_id, attempt);
        throw std::runtime_error(std::string("money_parse: ") + what);
    };

    try {
        const auto ctx = prompt_context(owner);
        const std::string hint_date = job->value("hint_date", std::string());
        const bool receipt = job->value("kind", std::string()) == "receipt";
        nlohmann::json user_message = ctx.message(hint_date);
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
            lines = Money::Parse::parse_answer(content, hint_date, ctx.own_accounts, ctx.own_categories);
        } catch (const Money::Parse::Invalid& e) {
            return failed("invalid_answer", e.what());
        }
        // A text job created with apply posts its lines in the transaction that closes it.
        jobs.finish_applying(
            job_id,
            attempt,
            Money::Parse::to_json(lines),
            body.contains("model") && body["model"].is_string() ? body["model"].get<std::string>() : model,
            Chat::usage(body, "prompt_tokens"),
            Chat::usage(body, "completion_tokens"),
            [&](auto& txn, const std::string& row_owner, const std::optional<std::string>& account_id) {
                nlohmann::json skipped = nlohmann::json::array();
                nlohmann::json created = apply_in(txn, row_owner, lines, account_id, "text", skipped);
                return nlohmann::json{{"money", created}, {"skipped", skipped}};
            });
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
