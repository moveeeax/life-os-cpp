/**
 * @file FitnessController.cpp
 * @brief Bodies for src/api/FitnessController.hpp — compiled once into app_core.
 */

#include "api/FitnessController.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <optional>
#include <stdexcept>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/RequestUtils.hpp"
#include "core/Modules.hpp"
#include "domain/Role.hpp"
#include "fitness/Export.hpp"
#include "fitness/sync/SyncService.hpp"
#include "fitness/xiaomi/CloudClient.hpp"
#include "fitness/xiaomi/DataKeys.hpp"
#include "fitness/xiaomi/Regions.hpp"
#include "fitness/xiaomi/Service.hpp"
#include "jobs/FitnessSyncHandler.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/fitness/CredentialsRepository.hpp"
#include "repositories/fitness/SyncRunRepository.hpp"
#include "utils/Config.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

using json = nlohmann::json;

namespace {

constexpr long kDefaultLimit = 1000;
constexpr long kMaxLimit = 10000;

/// Смещение суток региона: cn это UTC+8, прочие UTC (правило границ синка).
long long region_offset_seconds() {
    std::string region;
    if (Config::is_initialized()) {
        region = Config::get().get<std::string>("fitness.xiaomi.region", "MI_FITNESS_REGION", "");
    }
    return (region.empty() || region == "cn") ? 8 * 3600 : 0;
}

}  // namespace

// Гарды. Порядок: модуль → право → параметры. Макросы Guards.hpp заканчивают
// метод сами, поэтому они вызываются в теле метода, а не в хелпере.
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
        // Смещение пояса на проверку формата не влияет.
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
        // База лежит: состояние инфраструктуры, не 500 без следа в логе.
        spdlog::warn("fitness data read unavailable: {}", e.what());
        callback(ErrorResponse::service_unavailable("data_unavailable"));
    }
}

// ── probe ────────────────────────────────────────────────────────────────────

void FitnessController::probe(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    FITNESS_GUARD(req, callback, Domain::Permission::kFitnessSync);
    // Синк и probe не живут одновременно: оба логинятся, а каждый логин
    // ротирует passToken. Живой запуск в журнале — probe отказывается
    // до похода в облако.
    try {
        if (Repositories::SyncRunRepository().any_running()) {
            callback(ErrorResponse::conflict("sync_in_progress", "a sync run is in progress, retry later"));
            return;
        }
    } catch (const std::exception&) {
        // Недоступная база не должна прятать probe: он сам упрётся в неё
        // ниже и ответит честнее.
    }

    const std::string key = req->getParameter("key");
    const std::string from = req->getParameter("from");
    const std::string to = req->getParameter("to");

    // Ошибки клиента отсеиваются до учётных данных и до сети: опечатка в
    // ключе или дате это 400, а не 503.
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

    try {
        Repositories::CredentialsRepository repository(token_key);
        const auto credentials = repository.load();
        if (!credentials.has_value()) {
            callback(ErrorResponse::service_unavailable("not_configured", "Xiaomi credentials are not seeded yet"));
            return;
        }

        Xiaomi::CloudClient client(Xiaomi::Service::transport(),
                                   *credentials,
                                   [&repository](const Xiaomi::Credentials& rotated) { repository.store(rotated); });
        client.login();
        const auto items = client.fetch_key(key, from, to, std::nullopt);

        callback(Response::ok(json{{"data",
                                    {{"account", Xiaomi::mask_account_id(client.credentials().user_id)},
                                     {"region", client.credentials().region},
                                     {"key", key},
                                     {"records", items.size()}}}}));
    } catch (const Xiaomi::MiFitnessAuthError& e) {
        // Лечится только свежим токеном, поэтому код отличим от прочих
        // отказов. Тексты MiFitness*Error по построению не содержат значений.
        spdlog::warn("fitness probe auth failure: {}", e.what());
        callback(ErrorResponse::service_unavailable("upstream_auth", "Xiaomi refused the stored credentials"));
    } catch (const Xiaomi::MiFitnessProtocolError& e) {
        spdlog::warn("fitness probe protocol failure: {}", e.what());
        callback(ErrorResponse::service_unavailable("upstream_protocol",
                                                    "Xiaomi response did not match the expected format"));
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
                // Дубль прогнал бы тип дважды и затёр запись результата.
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
        // База или очередь лежат: это состояние инфраструктуры, а не 500
        // без следа в логе.
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

// ── чтение ───────────────────────────────────────────────────────────────────

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
    // Окна сна и resting-пульса строятся от локальной полуночи в поясе
    // региона: date у активности локальная (то же правило, что у границ
    // диапазона синка).
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
    // Без потолка ширины json_agg собирает годы данных одним значением в
    // памяти пода.
    const auto range = Xiaomi::range_to_timestamps(q.from, q.to, "cn");
    if (range.second - range.first > 366LL * 86400) {
        callback(ErrorResponse::bad_request("range_too_wide", "export covers at most 366 days per request"));
        return;
    }
    if (format == "csv" && type.empty()) {
        // CSV это плоская таблица одного типа; все типы разом это JSON.
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
