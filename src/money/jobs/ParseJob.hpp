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
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "jobs/RowJob.hpp"
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
    Repositories::Money::ParseJobRepository jobs;
    Jobs::RowJob<Repositories::Money::ParseJobRepository> job(
        kJobType, "money parse", "job_id", payload["job_id"].get<std::string>(), jobs, payload);

    if (!Core::money_enabled()) {
        return job.failed("money_disabled", "the money module is off on this worker");
    }
    const auto settings = Money::Llm::parse_settings();
    if (!settings.has_value()) {
        return job.failed("not_configured", "MONEY_LLM_* settings are missing");
    }
    const auto row = jobs.load_for_worker(job.id());
    if (!row.has_value()) {
        return {{"job_id", job.id()}, {"status", "missing"}};
    }
    if (const auto settled = job.claim()) {
        return *settled;
    }
    const std::string owner = row->value("owner_id", std::string());

    return job.run([&]() -> nlohmann::json {
        const auto ctx = prompt_context(owner);
        const std::string hint_date = row->value("hint_date", std::string());
        const bool receipt = row->value("kind", std::string()) == "receipt";
        nlohmann::json user_message = ctx.message(hint_date);
        nlohmann::json user_content;
        std::string model = settings->model;
        if (receipt) {
            const std::string image = (*row)["image"].is_string() ? (*row)["image"].get<std::string>() : "";
            if (image.empty()) {
                return job.failed("invalid_job", "the receipt has no photo");
            }
            user_content = nlohmann::json::array(
                {{{"type", "text"}, {"text", user_message.dump()}},
                 {{"type", "image_url"},
                  {"image_url",
                   {{"url", "data:" + row->value("image_type", std::string("image/jpeg")) + ";base64," + image}}}}});
            model = Money::Llm::vision_model(*settings);
        } else {
            user_message["text"] = row->value("text", std::string());
            user_content = user_message.dump();
        }

        std::variant<Chat::Answer, Chat::Failure> asked;
        try {
            asked = Chat::ask(Net::Http::transport(), *settings, Chat::json_request(*settings, user_content, model));
        } catch (const Chat::Retryable& e) {
            return job.give_up_or_retry("provider_unavailable", e.what());
        }
        if (const auto* failure = std::get_if<Chat::Failure>(&asked)) {
            return job.failed(failure->code, failure->message);
        }
        const auto& answer = std::get<Chat::Answer>(asked);
        std::vector<Money::Parse::Line> lines;
        try {
            lines = Money::Parse::parse_answer(answer.content, hint_date, ctx.own_accounts, ctx.own_categories);
        } catch (const Money::Parse::Invalid& e) {
            return job.failed("invalid_answer", e.what());
        }
        // A text job created with apply posts its lines in the transaction that closes it.
        jobs.finish_applying(
            job.id(),
            job.attempt(),
            Money::Parse::to_json(lines),
            answer.model,
            answer.prompt_tokens,
            answer.completion_tokens,
            [&](auto& txn, const std::string& row_owner, const std::optional<std::string>& account_id) {
                nlohmann::json skipped = nlohmann::json::array();
                nlohmann::json created = apply_in(txn, row_owner, lines, account_id, "text", skipped);
                return nlohmann::json{{"money", created}, {"skipped", skipped}};
            });
        return {{"job_id", job.id()}, {"status", "done"}, {"lines", lines.size()}};
    });
}

}  // namespace Jobs::MoneyParse
