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
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "food/jobs/ParseJob.hpp"
#include "jobs/RowJob.hpp"
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
    Repositories::UnifiedParse::ParseJobRepository jobs;
    Jobs::RowJob<Repositories::UnifiedParse::ParseJobRepository> job(
        kJobType, "unified parse", "job_id", payload["job_id"].get<std::string>(), jobs, payload);

    const auto setup = ::UnifiedParse::setup();
    if (!setup.has_value()) {
        return job.failed("not_configured", "no module parse is on and set up on this worker");
    }
    const auto loaded = jobs.load_for_worker(job.id());
    if (!loaded.has_value()) {
        return {{"job_id", job.id()}, {"status", "missing"}};
    }
    if (const auto settled = job.claim()) {
        return *settled;
    }
    const std::string owner = loaded->value("owner_id", std::string());

    return job.run([&]() -> nlohmann::json {
        const std::string date = loaded->value("date", std::string());
        const std::string meal = loaded->value("meal", std::string());
        ::UnifiedParse::Known known;
        known.date = date;
        nlohmann::json user_message{{"text", loaded->value("text", std::string())}};
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

        std::variant<Chat::Answer, Chat::Failure> asked;
        try {
            asked = Chat::ask(Net::Http::transport(), settings, Chat::json_request(settings, user_message.dump()));
        } catch (const Chat::Retryable& e) {
            return job.give_up_or_retry("provider_unavailable", e.what());
        }
        if (const auto* failure = std::get_if<Chat::Failure>(&asked)) {
            return job.failed(failure->code, failure->message);
        }
        const auto& answer = std::get<Chat::Answer>(asked);
        ::UnifiedParse::Parts parts;
        try {
            parts = ::UnifiedParse::parse_answer(answer.content, *setup, known);
        } catch (const ::UnifiedParse::Invalid& e) {
            return job.failed("invalid_answer", e.what());
        }
        const std::string& model = answer.model;
        const nlohmann::json food_lines = ::Food::Parse::to_json(parts.food);
        const nlohmann::json money_lines = ::Money::Parse::to_json(parts.money);
        const nlohmann::json task_lines = ::Tasks::Parse::to_json(parts.tasks);

        jobs.finish(job.id(),
                    job.attempt(),
                    model,
                    answer.prompt_tokens,
                    answer.completion_tokens,
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
        return {{"job_id", job.id()},
                {"status", "done"},
                {"food", parts.food.size()},
                {"money", parts.money.size()},
                {"tasks", parts.tasks.size()}};
    });
}

}  // namespace Jobs::UnifiedParse
