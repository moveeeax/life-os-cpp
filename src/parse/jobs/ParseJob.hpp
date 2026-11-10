/**
 * @file ParseJob.hpp
 * @brief unified_parse job handler: one phrase to the LLM, the rows of every
 *        kind it holds (food, money, tasks, water) into the job row.
 *
 * Payload: {job_id, owner_id, max_attempts}. The request carries what each
 * module's own parse sends (food items and profile note, accounts, categories
 * and merchants, the six areas), each under its kind's name (parse/Unified.hpp
 * builds the prompt and checks the answer).
 *
 * When the job ends, each kind's lines are filed as a finished child row of
 * that module's job table (food_parse_jobs, money_parse_jobs,
 * task_parse_jobs), so the module's accept route takes them as from its own
 * parse. A job created with apply also creates the rows in that transaction:
 * spends by the money rules (money/jobs/ParseApply.hpp), open tasks, diary
 * entries of the job's meal and date, the water of the day.
 *
 * Outcomes as in the module parses: a refusal, a bad answer, a missing
 * configuration close the job as failed; an outage requeues it until the
 * queue's last attempt, which closes it as failed.
 */

#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "food/jobs/ParseJob.hpp"
#include "jobs/Jobs.hpp"
#include "llm/Chat.hpp"
#include "money/jobs/ParseApply.hpp"
#include "money/jobs/ParseJob.hpp"
#include "net/Http.hpp"
#include "parse/Unified.hpp"
#include "repositories/food/EntryRepository.hpp"
#include "repositories/food/ParseJobRepository.hpp"
#include "repositories/food/WaterRepository.hpp"
#include "repositories/money/ParseJobRepository.hpp"
#include "repositories/parse/ParseJobRepository.hpp"
#include "repositories/tasks/ParseJobRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "tasks/jobs/ParseApply.hpp"
#include "tasks/jobs/ParseJob.hpp"

namespace Jobs::UnifiedParse {

inline constexpr const char* kJobType = ::UnifiedParse::kJobType;

namespace detail {

/// The diary entries of @p lines on the job's day and meal; returns `[{id}]`.
template <typename Txn>
nlohmann::json food_in(Txn& txn,
                       const std::string& owner,
                       const std::string& date,
                       const std::string& meal,
                       const std::vector<::Food::Parse::Line>& lines) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& l : lines) {
        Repositories::EntryRepository::Input in;
        in.date = date;
        in.meal = meal;
        in.name = l.name;
        in.item_id = l.item_id;
        in.grams = l.grams;
        in.kcal = l.kcal;
        in.protein_g = l.protein_g;
        in.fat_g = l.fat_g;
        in.carbs_g = l.carbs_g;
        in.estimated = l.estimated;
        in.note = l.note;
        out.push_back({{"id", Repositories::EntryRepository::create_in(txn, owner, in)}});
    }
    return out;
}

}  // namespace detail

inline nlohmann::json process_job(const nlohmann::json& payload) {
    if (!payload.contains("job_id") || !payload["job_id"].is_string() || payload["job_id"].get<std::string>().empty()) {
        throw std::invalid_argument("unified_parse: payload has no job_id");
    }
    const std::string job_id = payload["job_id"].get<std::string>();
    Repositories::UnifiedParse::ParseJobRepository jobs;
    int attempt = 0;  // 0 until the claim: a fail before it is not fenced by an attempt
    const auto failed = [&](const std::string& code, const std::string& message) {
        jobs.fail(job_id, code, message, attempt);
        spdlog::warn("unified parse {}: {}", job_id, code);
        return nlohmann::json{{"job_id", job_id}, {"status", "failed"}, {"error", code}};
    };

    const auto setup = ::UnifiedParse::setup();
    if (!setup.has_value()) {
        return failed("not_configured", "no module parse is on and set up on this worker");
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
            throw Jobs::DeferJob("unified_parse: the row is running elsewhere", jobs.stale_in_ms(job_id));
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
        throw std::runtime_error(std::string("unified_parse: ") + what);
    };

    try {
        const std::string date = job->value("date", std::string());
        const std::string meal = job->value("meal", std::string());
        ::UnifiedParse::Known known;
        known.date = date;
        nlohmann::json user_message{{"text", job->value("text", std::string())}};
        if (setup->food) {
            const auto ctx = Jobs::FoodParse::prompt_context(owner);
            known.food_items = ctx.own_ids;
            user_message["food"] = ctx.message(meal, date);
        }
        if (setup->money) {
            const auto ctx = Jobs::MoneyParse::prompt_context(owner);
            known.accounts = ctx.own_accounts;
            known.categories = ctx.own_categories;
            user_message["money"] = ctx.message(date);
        }
        if (setup->tasks) {
            known.open_titles = Repositories::Tasks::TaskRepository().open_titles(owner);
            user_message["tasks"] = Jobs::TasksParse::prompt_context(date);
        }
        const auto& settings = setup->settings;
        const nlohmann::json request{{"model", settings.model},
                                     {"messages",
                                      {{{"role", "system"}, {"content", settings.prompt}},
                                       {{"role", "user"}, {"content", user_message.dump()}}}},
                                     {"max_tokens", settings.max_tokens},
                                     {"temperature", 0},
                                     {"response_format", {{"type", "json_object"}}}};

        nlohmann::json body;
        std::string error_code;
        try {
            body = Chat::complete_adapting(Net::Http::transport(), settings, request, error_code);
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
        ::UnifiedParse::Parts parts;
        try {
            parts = ::UnifiedParse::parse_answer(content, *setup, known);
        } catch (const ::UnifiedParse::Invalid& e) {
            return failed("invalid_answer", e.what());
        }
        const std::string model =
            body.contains("model") && body["model"].is_string() ? body["model"].get<std::string>() : settings.model;
        const nlohmann::json food_lines = ::Food::Parse::to_json(parts.food);
        const nlohmann::json money_lines = ::Money::Parse::to_json(parts.money);
        const nlohmann::json task_lines = ::Tasks::Parse::to_json(parts.tasks);

        jobs.finish(job_id,
                    attempt,
                    model,
                    Chat::usage(body, "prompt_tokens"),
                    Chat::usage(body, "completion_tokens"),
                    [&](auto& txn, const Repositories::UnifiedParse::ParseJobRepository::Row& row) {
                        nlohmann::json ids{{"food", nullptr}, {"money", nullptr}, {"tasks", nullptr}};
                        nlohmann::json applied{{"food", nlohmann::json::array()},
                                               {"money", nlohmann::json::array()},
                                               {"tasks", nlohmann::json::array()},
                                               {"water", nullptr},
                                               {"skipped", nlohmann::json::array()}};
                        if (!parts.food.empty()) {
                            ids["food"] = Repositories::ParseJobRepository::insert_done_in(
                                txn, row.owner, row.text, row.meal, row.date, food_lines, model);
                            if (row.apply) {
                                applied["food"] = detail::food_in(txn, row.owner, row.date, row.meal, parts.food);
                            }
                        }
                        if (!parts.money.empty()) {
                            const std::string child = Repositories::Money::ParseJobRepository::insert_done_in(
                                txn, row.owner, row.text, row.date, money_lines, model, row.apply);
                            ids["money"] = child;
                            if (row.apply) {
                                nlohmann::json skipped = nlohmann::json::array();
                                applied["money"] = Jobs::MoneyParse::apply_in(
                                    txn, row.owner, parts.money, row.account_id, "text", skipped);
                                for (const auto& s : skipped) {
                                    applied["skipped"].push_back(s);
                                }
                                Repositories::Money::ParseJobRepository::mark_applied_in(
                                    txn, child, {{"money", applied["money"]}, {"skipped", skipped}});
                            }
                        }
                        if (!parts.tasks.empty()) {
                            const std::string child = Repositories::Tasks::ParseJobRepository::insert_done_in(
                                txn, row.owner, row.text, row.date, task_lines, model, row.apply);
                            ids["tasks"] = child;
                            if (row.apply) {
                                applied["tasks"] = Jobs::TasksParse::apply_in(txn, row.owner, parts.tasks);
                                Repositories::Tasks::ParseJobRepository::mark_applied_in(
                                    txn, child, {{"tasks", applied["tasks"]}, {"skipped", nlohmann::json::array()}});
                            }
                        }
                        if (parts.water_ml && row.apply) {
                            Repositories::WaterRepository::add_in(txn, row.owner, row.date, *parts.water_ml);
                            applied["water"] = {{"ml", *parts.water_ml}};
                        }
                        const nlohmann::json result{
                            {"food", food_lines},
                            {"money", money_lines},
                            {"tasks", task_lines},
                            {"water", parts.water_ml ? nlohmann::json{{"ml", *parts.water_ml}} : nlohmann::json()},
                            {"jobs", ids},
                            {"errors", parts.errors}};
                        return nlohmann::json{{"result", result}, {"applied", row.apply ? applied : nlohmann::json()}};
                    });
        return {{"job_id", job_id},
                {"status", "done"},
                {"food", parts.food.size()},
                {"money", parts.money.size()},
                {"tasks", parts.tasks.size()}};
    } catch (const std::runtime_error& e) {
        // give_up_or_retry's own throw: the queue retries.
        if (std::string(e.what()).rfind("unified_parse: ", 0) == 0) {
            throw;
        }
        return give_up_or_retry("internal_error", e.what());
    } catch (const std::exception& e) {
        return give_up_or_retry("internal_error", e.what());
    }
}

}  // namespace Jobs::UnifiedParse
