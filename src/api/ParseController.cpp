/**
 * @file ParseController.cpp
 * @brief Bodies for src/api/ParseController.hpp.
 */

#include "api/ParseController.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/FieldChecks.hpp"
#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "api/Validation.hpp"
#include "jobs/Jobs.hpp"
#include "jobs/Outbox.hpp"
#include "parse/Unified.hpp"
#include "repositories/JobRows.hpp"
#include "repositories/Zones.hpp"
#include "repositories/parse/ParseJobRepository.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Api::Fields::date_field;
using Api::Fields::idempotency_key;
using Api::Fields::text_length;
using Api::Fields::today_utc;
using Repo = Repositories::UnifiedParse::ParseJobRepository;

const std::vector<std::string> kMeals = {"breakfast", "lunch", "dinner", "snack"};

bool any_module_on() {
    return Core::food_enabled() || Core::money_enabled() || Core::tasks_enabled();
}

HttpResponsePtr too_many() {
    return ErrorResponse::make({k429TooManyRequests,
                                "too_many_parses",
                                "wait for your running parses to finish",
                                json{{"retry_after_sec", 30}}});
}

}  // namespace

void ParseController::start(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(any_module_on(), "parse", req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "text");
    text_length(errs, body, "text", 1, UnifiedParse::kTextMax);
    if (body.contains("meal") && !body["meal"].is_null()) {
        Validation::one_of(errs, body, "meal", kMeals);
    }
    date_field(errs, body, "date");
    if (body.contains("tz") && !body["tz"].is_null() && !body["tz"].is_string()) {
        errs.add("tz", "invalid", "must be an IANA zone name");
    }
    Validation::boolean(errs, body, "apply");
    if (body.contains("account_id") && !body["account_id"].is_null()) {
        Validation::uuid(errs, body, "account_id");
    }
    const auto key = idempotency_key(req, errs);
    if (!errs.any() && body["text"].get<std::string>().find('\0') != std::string::npos) {
        errs.add("text", "invalid", "must not contain a NUL character");
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    // The zone of the body, else ?tz=, as the module routes read it.
    std::string tz = Validation::opt_string(body, "tz").value_or(req->getParameter("tz"));
    std::optional<std::string> today;
    std::optional<int> hour;
    if (!tz.empty()) {
        try {
            today = Repositories::Zones::today_in(tz);
            hour = Repositories::Zones::hour_in(tz);
        } catch (const std::exception&) {
            // The zone list could not be read: the day falls back to UTC, as local_today does.
            tz.clear();
        }
        if (!tz.empty() && !today) {
            errs.add("tz", "invalid", "must be an IANA zone name");
            callback(Validation::response_400(errs));
            return;
        }
    }
    const auto setup = UnifiedParse::setup();
    if (!setup.has_value()) {
        callback(ErrorResponse::service_unavailable("not_configured", "no parse is set up on this server"));
        return;
    }
    const std::string text = body["text"].get<std::string>();
    const std::string date = Validation::opt_string(body, "date").value_or(today.value_or(today_utc()));
    // A phrase without a meal is filed by the hour on the person's clock when it reaches the server;
    // a client that queues phrases offline sends the meal of the moment it was typed.
    const std::string meal =
        Validation::opt_string(body, "meal").value_or(hour ? UnifiedParse::meal_at(*hour) : "snack");
    Repo::Apply apply;
    apply.apply = body.contains("apply") && body["apply"].is_boolean() && body["apply"].get<bool>();
    apply.account_id = Validation::opt_string(body, "account_id");
    apply.idempotency_key = key;

    const auto accepted = [&](const json& job) {
        auto resp = Response::ok(json{{"data", {{"id", job["id"]}, {"status", job["status"]}}}});
        resp->setStatusCode(k202Accepted);
        callback(resp);
    };
    // A retry of a request already taken answers with its job, before the cap.
    if (key) {
        std::optional<json> earlier;
        if (!with_repo_errors(
                callback, "parse.keyed", [&] { earlier = Repositories::JobRows::keyed(Repo::kTable, owner, *key); })) {
            return;
        }
        if (earlier) {
            accepted(*earlier);
            return;
        }
    }
    Repo repo;
    std::string id;
    bool replayed = false;
    // The job carries the retry limit it was submitted with.
    const int max_attempts = Jobs::is_initialized() ? Jobs::get().default_max_retries() : Jobs::kDefaultMaxRetries;
    const auto payload = [&](const std::string& job_id) {
        return json{{"job_id", job_id}, {"owner_id", owner}, {"max_attempts", max_attempts}};
    };
    // The job rides the row's transaction as an outbox row when a drain is configured
    // (Jobs::Outbox::enqueue_if_drained), as in the module parses.
    bool via_outbox = false;
    try {
        // One phrase is one job and one place among the kOpenMax open unified parses; it takes
        // no place in the module parses' own caps (its child rows are filed already done).
        if (repo.open_count(owner) >= Repositories::JobRows::kOpenMax) {
            callback(too_many());
            return;
        }
        const json job =
            repo.create(owner, text, meal, date, apply, replayed, [&](auto& txn, const std::string& job_id) {
                // The cap again, under the lock: two creates at once cannot both pass the count above.
                Repositories::JobRows::hold_slot(txn, Repo::kTable, owner);
                via_outbox = Jobs::Outbox::enqueue_if_drained(txn, UnifiedParse::kJobType, payload(job_id));
            });
        if (replayed) {
            accepted(job);
            return;
        }
        id = job["id"].get<std::string>();
    } catch (const Repositories::JobRows::SlotsTaken&) {
        callback(too_many());
        return;
    } catch (const std::exception&) {
        // The server's DETAIL would quote the phrase: no message.
        spdlog::warn("unified parse create failed");
        callback(ErrorResponse::service_unavailable("storage_unavailable"));
        return;
    }
    try {
        if (!via_outbox) {
            if (!Jobs::is_initialized()) {
                throw std::runtime_error("the job queue is not initialized");
            }
            Jobs::get().submit(UnifiedParse::kJobType, payload(id));
        }
    } catch (const std::exception& e) {
        spdlog::warn("unified parse enqueue unavailable: {}", e.what());
        try {
            repo.fail(id, "queue_unavailable", "the job queue did not take the job");
        } catch (const std::exception& inner) {
            spdlog::warn("unified parse {}: could not mark failed: {}", id, inner.what());
        }
        callback(ErrorResponse::service_unavailable("queue_unavailable"));
        return;
    }
    auto resp = Response::ok(json{{"data", {{"id", id}, {"status", "queued"}}}});
    resp->setStatusCode(k202Accepted);
    callback(resp);
}

void ParseController::status(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(any_module_on(), "parse", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "parse.status", [&] {
        const auto row = Repo().get(owner, id);
        if (!row) {
            callback(ErrorResponse::not_found("parse_job"));
            return;
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

}  // namespace Api
