/**
 * @file FitnessController.hpp
 * @brief Модуль fitness: probe облака, постановка синка, чтение данных здоровья.
 *
 * Все маршруты под /api/v1/fitness. Порядок проверок в каждом методе:
 * выключенный модуль → 404 `fitness`; затем бит права (kFitnessSync для
 * probe и sync, kFitnessRead для чтения) → 403; затем разбор параметров → 400.
 *
 * Declarations only — the handler bodies live in FitnessController.cpp
 * (compiled once into app_core; ADR 0003 as amended 2026-08-22). The route
 * macros (ADD_METHOD_TO) must stay in this header: Drogon's METHOD_LIST
 * registration is part of the class definition, and
 * scripts/check-routes-registered.sh greps the src/api headers for them.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

#include <nlohmann/json_fwd.hpp>

#include "repositories/fitness/HealthReadRepository.hpp"

namespace Api {

using namespace drogon;

class FitnessController : public HttpController<FitnessController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(FitnessController::probe, "/api/v1/fitness/probe", Get);
    ADD_METHOD_TO(FitnessController::syncEnqueue, "/api/v1/fitness/sync", Post);
    ADD_METHOD_TO(FitnessController::syncStatus, "/api/v1/fitness/sync/{id}", Get);
    ADD_METHOD_TO(FitnessController::dailyActivity, "/api/v1/fitness/daily-activity", Get);
    ADD_METHOD_TO(FitnessController::sleep, "/api/v1/fitness/sleep", Get);
    ADD_METHOD_TO(FitnessController::heartRate, "/api/v1/fitness/heart-rate", Get);
    ADD_METHOD_TO(FitnessController::stress, "/api/v1/fitness/stress", Get);
    ADD_METHOD_TO(FitnessController::spo2, "/api/v1/fitness/spo2", Get);
    ADD_METHOD_TO(FitnessController::body, "/api/v1/fitness/body", Get);
    ADD_METHOD_TO(FitnessController::workouts, "/api/v1/fitness/workouts", Get);
    ADD_METHOD_TO(FitnessController::summary, "/api/v1/fitness/summary", Get);
    ADD_METHOD_TO(FitnessController::abnormalHeartBeat, "/api/v1/fitness/abnormal-heart-beat", Get);
    ADD_METHOD_TO(FitnessController::coverage, "/api/v1/fitness/coverage", Get);
    ADD_METHOD_TO(FitnessController::exportData, "/api/v1/fitness/export", Get);
    METHOD_LIST_END

    void probe(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void syncEnqueue(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void syncStatus(const HttpRequestPtr& req,
                    std::function<void(const HttpResponsePtr&)>&& callback,
                    const std::string& id);
    void dailyActivity(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void sleep(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void heartRate(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void stress(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void spo2(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void body(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void workouts(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void summary(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void abnormalHeartBeat(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void coverage(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void exportData(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);

private:
    /// Разобранные параметры запроса чтения.
    struct Query {
        std::string from;
        std::string to;
        long limit = 1000;
        long offset = 0;
    };

    /// 404, если модуль выключен. Возвращает false после ответа.
    static bool require_enabled(const std::function<void(const HttpResponsePtr&)>& callback);

    /// Разобрать from/to/limit/offset. На ошибке отвечает 400 и возвращает false.
    static bool parse_query(const HttpRequestPtr& req,
                            Query& query,
                            const std::function<void(const HttpResponsePtr&)>& callback);

    /// Общий хвост чтения: выполнить и завернуть страницу в ответ.
    static void respond_page(const std::function<Repositories::HealthReadRepository::Page()>& read,
                             const std::function<void(const HttpResponsePtr&)>& callback);
};

}  // namespace Api
