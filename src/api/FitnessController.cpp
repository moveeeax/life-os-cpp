/**
 * @file FitnessController.cpp
 * @brief Bodies for src/api/FitnessController.hpp — compiled once into app_core.
 */

#include "api/FitnessController.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <optional>
#include <stdexcept>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "core/Modules.hpp"
#include "domain/Role.hpp"
#include "fitness/Export.hpp"
#include "fitness/LinkService.hpp"
#include "fitness/sync/SyncService.hpp"
#include "fitness/xiaomi/CloudClient.hpp"
#include "fitness/xiaomi/DataKeys.hpp"
#include "fitness/xiaomi/RegionDetect.hpp"
#include "fitness/xiaomi/Regions.hpp"
#include "fitness/xiaomi/Service.hpp"
#include "jobs/FitnessSyncHandler.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/fitness/MiAccountRepository.hpp"
#include "repositories/fitness/SyncRunRepository.hpp"
#include "utils/Config.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

using json = nlohmann::json;

namespace {

constexpr long kDefaultLimit = 1000;
constexpr long kMaxLimit = 10000;

/// Region day offset: cn is UTC+8, others UTC (same rule as sync range bounds).
/// The region is the one of the linked account.
long long region_offset_seconds() {
    std::string region = "cn";
    try {
        region = Repositories::MiAccountRepository("").first_region().value_or("cn");
    } catch (const std::exception&) {
        // An unreachable database is reported by the query that follows.
    }
    return (region.empty() || region == "cn") ? 8 * 3600 : 0;
}

/// Rows belong to an app user; a static-bearer principal has no user id.
bool require_user(const std::string& owner, const std::function<void(const HttpResponsePtr&)>& callback) {
    if (is_valid_uuid(owner)) {
        return true;
    }
    callback(ErrorResponse::forbidden("no_user_account", "this route needs a user account"));
    return false;
}

/// now + offset as an ISO 8601 instant in UTC.
std::string iso_from_now(long seconds) {
    const auto at =
        std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) + std::chrono::seconds(seconds);
    const auto day = std::chrono::floor<std::chrono::days>(at);
    const std::chrono::year_month_day ymd{day};
    const std::chrono::hh_mm_ss hms{at - day};
    char out[32];
    std::snprintf(out,
                  sizeof(out),
                  "%04d-%02u-%02uT%02d:%02d:%02d+00:00",
                  static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()),
                  static_cast<unsigned>(ymd.day()),
                  static_cast<int>(hms.hours().count()),
                  static_cast<int>(hms.minutes().count()),
                  static_cast<int>(hms.seconds().count()));
    return out;
}

/// The caller's link as the API shows it: {status: "none"} without a link,
/// otherwise the repository's status with the Xiaomi id masked.
json account_status_body(const std::string& owner) {
    auto status = Repositories::MiAccountRepository("").status(owner);
    if (!status.has_value()) {
        return json{{"status", "none"}};
    }
    json out = std::move(*status);
    out["account"] = Xiaomi::mask_account_id(out.value("xiaomi_user_id", std::string()));
    out.erase("xiaomi_user_id");
    return out;
}

}  // namespace

// Guards. Order: module -> permission -> parameters. The Guards.hpp macros
// return from the method themselves, so they are invoked in the method body, not in a helper.
#define FITNESS_GUARD(req, callback, perm)           \
    do {                                             \
        if (!require_enabled(callback))              \
            return;                                  \
        API_REQUIRE_PERMISSION(req, callback, perm); \
    } while (0)

bool FitnessController::require_enabled(const std::function<void(const HttpResponsePtr&)>& callback) {
    if (Core::fitness_enabled())
        return true;
    callback(ErrorResponse::not_found("fitness"));
    return false;
}

bool FitnessController::parse_query(const HttpRequestPtr& req,
                                    Query& query,
                                    const std::function<void(const HttpResponsePtr&)>& callback) {
    query.from = req->getParameter("from");
    query.to = req->getParameter("to");
    if (query.from.empty() || query.to.empty()) {
        callback(ErrorResponse::bad_request("invalid_range", "from and to are required as YYYY-MM-DD"));
        return false;
    }
    try {
        // The zone offset does not affect the format check.
        Xiaomi::range_to_timestamps(query.from, query.to, "cn");
    } catch (const Xiaomi::MiFitnessProtocolError&) {
        callback(ErrorResponse::bad_request("invalid_range",
                                            "from and to must be YYYY-MM-DD and from must not be after to"));
        return false;
    }
    query.limit = kDefaultLimit;
    query.offset = 0;
    const std::string limit = req->getParameter("limit");
    const std::string offset = req->getParameter("offset");
    try {
        if (!limit.empty()) {
            query.limit = std::stol(limit);
        }
        if (!offset.empty()) {
            query.offset = std::stol(offset);
        }
    } catch (const std::exception&) {
        callback(ErrorResponse::bad_request("invalid_pagination", "limit and offset must be integers"));
        return false;
    }
    if (query.limit < 1 || query.limit > kMaxLimit || query.offset < 0) {
        callback(
            ErrorResponse::bad_request("invalid_pagination", "limit must be 1..10000 and offset must not be negative"));
        return false;
    }
    return true;
}

void FitnessController::respond_page(const std::function<Repositories::HealthReadRepository::Page()>& read,
                                     const std::function<void(const HttpResponsePtr&)>& callback) {
    try {
        const auto page = read();
        callback(Response::ok(json{{"data", page.rows}, {"count", page.rows.size()}, {"total", page.total}}));
    } catch (const std::exception& e) {
        // Database is down: an infrastructure state, not a 500 with no trace in the log.
        spdlog::warn("fitness data read unavailable: {}", e.what());
        callback(ErrorResponse::service_unavailable("data_unavailable"));
    }
}

// ── probe ────────────────────────────────────────────────────────────────────

void FitnessController::probe(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessSync);
    API_REQUIRE_OWNER(req, callback, owner);
    if (!require_user(owner, callback)) {
        return;
    }
    // Sync and probe cannot run together: both log in, and every login
    // rotates the passToken. A live run in the journal means probe refuses
    // before going to the cloud.
    try {
        if (Repositories::SyncRunRepository().any_running()) {
            callback(ErrorResponse::conflict("sync_in_progress", "a sync run is in progress, retry later"));
            return;
        }
    } catch (const std::exception&) {
        // An unreachable database must not hide the probe: it hits the
        // database itself below and gives a more honest answer.
    }

    const std::string key = req->getParameter("key");
    const std::string from = req->getParameter("from");
    const std::string to = req->getParameter("to");

    // Client errors are filtered out before credentials and before the
    // network: a typo in the key or date is a 400, not a 503.
    if (!Xiaomi::is_known_data_key(key)) {
        callback(ErrorResponse::bad_request("unknown_key", "key must be one of the Mi Fitness data keys"));
        return;
    }
    try {
        Xiaomi::range_to_timestamps(from, to, "cn");
    } catch (const Xiaomi::MiFitnessProtocolError&) {
        callback(ErrorResponse::bad_request("invalid_range",
                                            "from and to must be YYYY-MM-DD and from must not be after to"));
        return;
    }

    const std::string token_key = Xiaomi::Service::token_key_b64();
    if (token_key.empty()) {
        callback(ErrorResponse::service_unavailable("not_configured", "MI_FITNESS_TOKEN_KEY is not set"));
        return;
    }

    Repositories::MiAccountRepository accounts(token_key);
    try {
        const auto credentials = accounts.load(owner);
        if (!credentials.has_value()) {
            callback(ErrorResponse::conflict("not_linked", "no Mi account is linked"));
            return;
        }

        Xiaomi::CloudClient client(
            Xiaomi::Service::transport(), *credentials, [&accounts, &owner](const Xiaomi::Credentials& rotated) {
                accounts.store_rotated(owner, rotated);
            });
        client.login();
        accounts.mark_ok(owner);
        const auto items = client.fetch_key(key, from, to, std::nullopt);

        callback(Response::ok(json{{"data",
                                    {{"account", Xiaomi::mask_account_id(client.credentials().user_id)},
                                     {"region", client.credentials().region},
                                     {"key", key},
                                     {"records", items.size()}}}}));
    } catch (const Xiaomi::MiFitnessAuthError& e) {
        // Only a fresh token fixes this, so the code is distinct from other
        // failures. MiFitness*Error messages contain no values by construction.
        spdlog::warn("fitness probe auth failure: {}", e.what());
        try {
            accounts.mark_reauth_required(owner, "upstream_auth");
        } catch (const std::exception& mark_error) {
            spdlog::warn("fitness probe: failed to record the link status: {}", mark_error.what());
        }
        callback(ErrorResponse::service_unavailable("upstream_auth", "Xiaomi refused the stored credentials"));
    } catch (const Xiaomi::MiFitnessProtocolError& e) {
        spdlog::warn("fitness probe protocol failure: {}", e.what());
        callback(ErrorResponse::service_unavailable("upstream_protocol",
                                                    "Xiaomi response did not match the expected format"));
    }
}

// ── account ──────────────────────────────────────────────────────────────────

// The Mi account routes act on the caller's own link: module -> fitness:sync
// -> a user account. API_REQUIRE_OWNER declares `owner`, hence no do/while.
#define ACCOUNT_GUARD(req, callback, owner)                         \
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessSync); \
    API_REQUIRE_OWNER(req, callback, owner);                        \
    if (!require_user(owner, callback))                             \
    return

void FitnessController::accountStatus(const HttpRequestPtr& req,
                                      std::function<void(const HttpResponsePtr&)>&& callback) {
    ACCOUNT_GUARD(req, callback, owner);
    with_repo_errors(callback, "fitness.accountStatus", [&] {
        callback(Response::ok(json{{"data", account_status_body(owner)}}));
    });
}

void FitnessController::accountLinkStart(const HttpRequestPtr& req,
                                         std::function<void(const HttpResponsePtr&)>&& callback) {
    ACCOUNT_GUARD(req, callback, owner);
    try {
        const auto started = Fitness::Link::start(owner);
        callback(Response::created(json{{"data",
                                         {{"link_id", started.link_id},
                                          {"qr_png_base64", started.qr_png_base64},
                                          {"confirm_url", started.confirm_url},
                                          {"expires_in_seconds", started.expires_in_seconds},
                                          {"expires_at", iso_from_now(started.expires_in_seconds)}}}}));
    } catch (const Fitness::Link::NotConfigured&) {
        callback(ErrorResponse::service_unavailable("not_configured", "MI_FITNESS_TOKEN_KEY is not set"));
    } catch (const Fitness::Link::RateLimited&) {
        callback(ErrorResponse::too_many_requests(static_cast<int>(Fitness::Link::kStartWindowSeconds)));
    } catch (const std::exception& e) {
        // Xiaomi did not issue a sign-in, or the attempt could not be parked.
        // MiFitness*Error messages carry no values by construction.
        spdlog::warn("mi link start failed: {}", e.what());
        callback(ErrorResponse::service_unavailable("upstream_unavailable", "Xiaomi sign-in is not available now"));
    }
}

void FitnessController::accountLinkStep(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback,
                                        const std::string& link_id) {
    ACCOUNT_GUARD(req, callback, owner);
    try {
        const auto step = Fitness::Link::step(owner, link_id);
        json data{{"state", "pending"}};
        if (step.state == Fitness::Link::State::Linked) {
            data["state"] = "linked";
            // First data for the new link. A failure to enqueue is not a
            // failure to link: the schedule or the user starts a sync later.
            try {
                const int window =
                    Config::get().get<int>("fitness.xiaomi.sync_window_days", "MI_FITNESS_SYNC_WINDOW_DAYS", 2);
                Jobs::FitnessSync::enqueue_recent(window, static_cast<long long>(::time(nullptr)));
            } catch (const std::exception& e) {
                spdlog::warn("mi link: first sync was not enqueued: {}", e.what());
            }
        } else if (step.state == Fitness::Link::State::Failed) {
            data["state"] = "failed";
            data["error"] = step.error_code;
        }
        callback(Response::ok(json{{"data", data}}));
    } catch (const Fitness::Link::LinkNotFound&) {
        callback(ErrorResponse::not_found("link_attempt"));
    } catch (const std::exception& e) {
        spdlog::error("fitness.accountLinkStep failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void FitnessController::accountUnlink(const HttpRequestPtr& req,
                                      std::function<void(const HttpResponsePtr&)>&& callback) {
    ACCOUNT_GUARD(req, callback, owner);
    // The body is optional; without it the data stays.
    bool delete_data = false;
    if (!req->body().empty()) {
        const json body = json::parse(std::string(req->body()), nullptr, /*allow_exceptions=*/false);
        if (body.is_discarded() || !body.is_object() ||
            (body.contains("delete_data") && !body["delete_data"].is_boolean())) {
            callback(ErrorResponse::bad_request("invalid_body", "delete_data must be a boolean"));
            return;
        }
        delete_data = body.value("delete_data", false);
    }
    with_repo_errors(callback, "fitness.accountUnlink", [&] {
        // A running sync writes rows and may write a rotated token.
        if (Repositories::SyncRunRepository().any_running()) {
            callback(ErrorResponse::conflict("sync_in_progress", "a sync run is in progress, retry later"));
            return;
        }
        if (!Repositories::MiAccountRepository("").unlink(owner, delete_data)) {
            callback(ErrorResponse::not_found("mi_account"));
            return;
        }
        callback(Response::ok(json{{"message", "Mi account unlinked"}}));
    });
}

void FitnessController::accountPatch(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback) {
    ACCOUNT_GUARD(req, callback, owner);
    const json body = json::parse(std::string(req->body()), nullptr, /*allow_exceptions=*/false);
    if (body.is_discarded() || !body.is_object() || !body.contains("region") || !body["region"].is_string()) {
        callback(ErrorResponse::bad_request("invalid_body", "body must carry region"));
        return;
    }
    const std::string region = body["region"].get<std::string>();
    if (region.empty() || !Xiaomi::is_known_region(region)) {
        callback(ErrorResponse::bad_request("unknown_region", "region must be one of ru, cn, de, i2, sg, us"));
        return;
    }
    with_repo_errors(callback, "fitness.accountPatch", [&] {
        // Chosen by the user: treated as known, the selector goes away.
        if (!Repositories::MiAccountRepository("").set_region(owner, region, true)) {
            callback(ErrorResponse::not_found("mi_account"));
            return;
        }
        callback(Response::ok(json{{"data", account_status_body(owner)}}));
    });
}

void FitnessController::accountDetectRegion(const HttpRequestPtr& req,
                                            std::function<void(const HttpResponsePtr&)>&& callback) {
    ACCOUNT_GUARD(req, callback, owner);
    const std::string token_key = Xiaomi::Service::token_key_b64();
    if (token_key.empty()) {
        callback(ErrorResponse::service_unavailable("not_configured", "MI_FITNESS_TOKEN_KEY is not set"));
        return;
    }
    Repositories::MiAccountRepository accounts(token_key);
    try {
        // Detection logs in, and a login must not run next to a sync.
        if (Repositories::SyncRunRepository().any_running()) {
            callback(ErrorResponse::conflict("sync_in_progress", "a sync run is in progress, retry later"));
            return;
        }
        const auto credentials = accounts.load(owner);
        if (!credentials.has_value()) {
            callback(ErrorResponse::not_found("mi_account"));
            return;
        }
        Xiaomi::CloudClient client(
            Xiaomi::Service::transport(), *credentials, [&accounts, &owner](const Xiaomi::Credentials& rotated) {
                accounts.store_rotated(owner, rotated);
            });
        client.login();
        accounts.mark_ok(owner);
        const auto region = Xiaomi::detect_region(client, Fitness::Link::detail::today_utc());
        accounts.set_region(owner, region.value_or(credentials->region), region.has_value());
        callback(Response::ok(json{{"data", account_status_body(owner)}}));
    } catch (const Xiaomi::MiFitnessAuthError& e) {
        spdlog::warn("fitness region detection auth failure: {}", e.what());
        try {
            accounts.mark_reauth_required(owner, "upstream_auth");
        } catch (const std::exception& mark_error) {
            spdlog::warn("fitness region detection: failed to record the link status: {}", mark_error.what());
        }
        callback(ErrorResponse::service_unavailable("upstream_auth", "Xiaomi refused the stored credentials"));
    } catch (const Xiaomi::MiFitnessProtocolError& e) {
        spdlog::warn("fitness region detection protocol failure: {}", e.what());
        callback(ErrorResponse::service_unavailable("upstream_protocol",
                                                    "Xiaomi response did not match the expected format"));
    } catch (const std::exception& e) {
        spdlog::error("fitness.accountDetectRegion failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

// ── sync ─────────────────────────────────────────────────────────────────────

void FitnessController::syncEnqueue(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessSync);
    json body = json::parse(std::string(req->body()), nullptr, /*allow_exceptions=*/false);
    if (body.is_discarded() || !body.is_object() || !body.contains("from") || !body.contains("to") ||
        !body["from"].is_string() || !body["to"].is_string()) {
        callback(ErrorResponse::bad_request("invalid_body", "body must carry from and to as YYYY-MM-DD"));
        return;
    }
    const std::string from = body["from"].get<std::string>();
    const std::string to = body["to"].get<std::string>();
    try {
        Xiaomi::range_to_timestamps(from, to, "cn");
    } catch (const Xiaomi::MiFitnessProtocolError&) {
        callback(ErrorResponse::bad_request("invalid_range",
                                            "from and to must be YYYY-MM-DD and from must not be after to"));
        return;
    }
    std::vector<std::string> data_types = Sync::kAllDataTypes;
    if (body.contains("data_types")) {
        if (!body["data_types"].is_array() || body["data_types"].empty()) {
            callback(ErrorResponse::bad_request("invalid_data_types", "data_types must be a non-empty array"));
            return;
        }
        data_types.clear();
        for (const auto& item : body["data_types"]) {
            if (!item.is_string() ||
                std::find(Sync::kAllDataTypes.begin(), Sync::kAllDataTypes.end(), item.get<std::string>()) ==
                    Sync::kAllDataTypes.end()) {
                callback(ErrorResponse::bad_request("unknown_data_type", "data_types must be Mi Fitness types"));
                return;
            }
            if (std::find(data_types.begin(), data_types.end(), item.get<std::string>()) != data_types.end()) {
                // A duplicate would run the type twice and overwrite its result entry.
                callback(ErrorResponse::bad_request("duplicate_data_type", "data_types must not repeat"));
                return;
            }
            data_types.push_back(item.get<std::string>());
        }
    }

    try {
        Repositories::SyncRunRepository runs;
        const long run_id = runs.create(from, to, data_types);
        Jobs::get().submit(Jobs::FitnessSync::kJobType,
                           json{{"run_id", run_id}, {"from", from}, {"to", to}, {"data_types", data_types}});
        auto resp = Response::ok(json{{"data", {{"run_id", run_id}, {"status", "queued"}}}});
        resp->setStatusCode(k202Accepted);
        callback(resp);
    } catch (const std::exception& e) {
        // Database or queue is down: an infrastructure state, not a 500
        // with no trace in the log.
        spdlog::warn("fitness sync enqueue unavailable: {}", e.what());
        callback(ErrorResponse::service_unavailable("queue_unavailable"));
    }
}

void FitnessController::syncStatus(const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessSync);
    long run_id = 0;
    try {
        std::size_t consumed = 0;
        run_id = std::stol(id, &consumed);
        if (consumed != id.size() || run_id <= 0) {
            throw std::invalid_argument("trailing garbage");
        }
    } catch (const std::exception&) {
        callback(ErrorResponse::bad_request("invalid_id", "run id must be a positive integer"));
        return;
    }
    try {
        const auto row = Repositories::SyncRunRepository().get(run_id);
        if (!row.has_value()) {
            callback(ErrorResponse::not_found("run_not_found"));
            return;
        }
        callback(Response::ok(json{{"data", *row}}));
    } catch (const std::exception& e) {
        spdlog::warn("fitness sync status unavailable: {}", e.what());
        callback(ErrorResponse::service_unavailable("journal_unavailable"));
    }
}

// ── reads ────────────────────────────────────────────────────────────────────

void FitnessController::dailyActivity(const HttpRequestPtr& req,
                                      std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    respond_page([q] { return Repositories::HealthReadRepository().daily_activity(q.from, q.to, q.limit, q.offset); },
                 callback);
}

void FitnessController::sleep(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    respond_page([q] { return Repositories::HealthReadRepository().sleep(q.from, q.to, q.limit, q.offset); }, callback);
}

void FitnessController::heartRate(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    const std::string type = req->getParameter("type");
    if (!type.empty() && type != "passive" && type != "active" && type != "resting" && type != "manual") {
        callback(ErrorResponse::bad_request("invalid_type", "type must be passive, active, resting or manual"));
        return;
    }
    respond_page(
        [q, type] { return Repositories::HealthReadRepository().heart_rate(q.from, q.to, type, q.limit, q.offset); },
        callback);
}

void FitnessController::stress(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    respond_page([q] { return Repositories::HealthReadRepository().stress(q.from, q.to, q.limit, q.offset); },
                 callback);
}

void FitnessController::spo2(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    respond_page([q] { return Repositories::HealthReadRepository().spo2(q.from, q.to, q.limit, q.offset); }, callback);
}

void FitnessController::body(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    respond_page([q] { return Repositories::HealthReadRepository().body(q.from, q.to, q.limit, q.offset); }, callback);
}

void FitnessController::workouts(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    respond_page([q] { return Repositories::HealthReadRepository().workouts(q.from, q.to, q.limit, q.offset); },
                 callback);
}

void FitnessController::summary(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    // Sleep and resting heart rate windows start at local midnight in the
    // region's zone: the activity date is local (same rule as the sync
    // range bounds).
    const long long offset = region_offset_seconds();
    respond_page(
        [q, offset] { return Repositories::HealthReadRepository().summary(q.from, q.to, q.limit, q.offset, offset); },
        callback);
}

void FitnessController::abnormalHeartBeat(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    Query q;
    if (!parse_query(req, q, callback))
        return;
    respond_page(
        [q] { return Repositories::HealthReadRepository().abnormal_heart_beat(q.from, q.to, q.limit, q.offset); },
        callback);
}

void FitnessController::coverage(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    try {
        callback(Response::ok(json{{"data", Repositories::HealthReadRepository().coverage()}}));
    } catch (const std::exception& e) {
        spdlog::warn("fitness coverage unavailable: {}", e.what());
        callback(ErrorResponse::service_unavailable("data_unavailable"));
    }
}

void FitnessController::exportData(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessRead);
    const auto& kTypes = Fitness::Export::types();
    Query q;
    if (!parse_query(req, q, callback))
        return;
    const std::string format = req->getParameter("format").empty() ? "json" : req->getParameter("format");
    const std::string type = req->getParameter("type");
    if (format != "json" && format != "csv") {
        callback(ErrorResponse::bad_request("invalid_format", "format must be json or csv"));
        return;
    }
    if (!type.empty() && std::find(kTypes.begin(), kTypes.end(), type) == kTypes.end()) {
        callback(ErrorResponse::bad_request("unknown_data_type", "type must be one of the exported datasets"));
        return;
    }
    // Without a width ceiling json_agg builds years of data as a single
    // value in the pod's memory.
    const auto range = Xiaomi::range_to_timestamps(q.from, q.to, "cn");
    if (range.second - range.first > 366LL * 86400) {
        callback(ErrorResponse::bad_request("range_too_wide", "export covers at most 366 days per request"));
        return;
    }
    if (format == "csv" && type.empty()) {
        // CSV is a flat table of one type; all types at once is JSON.
        callback(ErrorResponse::bad_request("csv_needs_type", "csv export takes exactly one type"));
        return;
    }
    try {
        Repositories::HealthReadRepository repo;
        if (format == "json") {
            json records = json::object();
            if (type.empty()) {
                for (const auto& t : kTypes) {
                    records[t] = repo.export_rows(t, q.from, q.to);
                }
            } else {
                records[type] = repo.export_rows(type, q.from, q.to);
            }
            const auto now =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count();
            callback(Response::ok(Fitness::Export::envelope(records, type, q.from, q.to, now)));
            return;
        }
        const auto rows = repo.export_rows(type, q.from, q.to);
        auto resp = HttpResponse::newHttpResponse();
        resp->setContentTypeString("text/csv; charset=utf-8");
        resp->setBody(Fitness::Export::to_csv(rows));
        callback(resp);
    } catch (const std::exception& e) {
        spdlog::warn("fitness export unavailable: {}", e.what());
        callback(ErrorResponse::service_unavailable("data_unavailable"));
    }
}

#undef FITNESS_GUARD

}  // namespace Api
