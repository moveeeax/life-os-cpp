/**
 * @file TravelController.hpp
 * @brief /api/v1/travel: the owner's border crossings, day counters, trips with
 *        their plan, packing and spends
 *        (docs/superpowers/specs/2026-10-13-travel-design.md).
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class TravelController : public HttpController<TravelController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TravelController::getSettings, "/api/v1/travel/settings", Get);
    ADD_METHOD_TO(TravelController::putSettings, "/api/v1/travel/settings", Put);
    ADD_METHOD_TO(TravelController::listCrossings, "/api/v1/travel/crossings", Get);
    ADD_METHOD_TO(TravelController::createCrossing, "/api/v1/travel/crossings", Post);
    ADD_METHOD_TO(TravelController::updateCrossing, "/api/v1/travel/crossings/{id}", Patch);
    ADD_METHOD_TO(TravelController::deleteCrossing, "/api/v1/travel/crossings/{id}", Delete);
    ADD_METHOD_TO(TravelController::listRules, "/api/v1/travel/rules", Get);
    ADD_METHOD_TO(TravelController::createRule, "/api/v1/travel/rules", Post);
    ADD_METHOD_TO(TravelController::addTemplate, "/api/v1/travel/rules/templates/{key}", Post);
    ADD_METHOD_TO(TravelController::updateRule, "/api/v1/travel/rules/{id}", Patch);
    ADD_METHOD_TO(TravelController::deleteRule, "/api/v1/travel/rules/{id}", Delete);
    ADD_METHOD_TO(TravelController::counters, "/api/v1/travel/counters", Get);
    ADD_METHOD_TO(TravelController::days, "/api/v1/travel/days", Get);
    ADD_METHOD_TO(TravelController::listTrips, "/api/v1/travel/trips", Get);
    ADD_METHOD_TO(TravelController::createTrip, "/api/v1/travel/trips", Post);
    ADD_METHOD_TO(TravelController::getTrip, "/api/v1/travel/trips/{id}", Get);
    ADD_METHOD_TO(TravelController::updateTrip, "/api/v1/travel/trips/{id}", Patch);
    ADD_METHOD_TO(TravelController::deleteTrip, "/api/v1/travel/trips/{id}", Delete);
    ADD_METHOD_TO(TravelController::addItem, "/api/v1/travel/trips/{id}/items", Post);
    ADD_METHOD_TO(TravelController::updateItem, "/api/v1/travel/trips/{id}/items/{child}", Patch);
    ADD_METHOD_TO(TravelController::deleteItem, "/api/v1/travel/trips/{id}/items/{child}", Delete);
    ADD_METHOD_TO(TravelController::candidateSpends, "/api/v1/travel/trips/{id}/candidate-spends", Get);
    ADD_METHOD_TO(TravelController::linkSpends, "/api/v1/travel/trips/{id}/spends", Post);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void getSettings(const HttpRequestPtr& req, Callback&& callback);
    void putSettings(const HttpRequestPtr& req, Callback&& callback);
    void listCrossings(const HttpRequestPtr& req, Callback&& callback);
    void createCrossing(const HttpRequestPtr& req, Callback&& callback);
    void updateCrossing(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteCrossing(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void listRules(const HttpRequestPtr& req, Callback&& callback);
    void createRule(const HttpRequestPtr& req, Callback&& callback);
    void addTemplate(const HttpRequestPtr& req, Callback&& callback, const std::string& key);
    void updateRule(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteRule(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void counters(const HttpRequestPtr& req, Callback&& callback);
    void days(const HttpRequestPtr& req, Callback&& callback);
    void listTrips(const HttpRequestPtr& req, Callback&& callback);
    void createTrip(const HttpRequestPtr& req, Callback&& callback);
    void getTrip(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void updateTrip(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteTrip(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void addItem(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void updateItem(const HttpRequestPtr& req, Callback&& callback, const std::string& id, const std::string& child);
    void deleteItem(const HttpRequestPtr& req, Callback&& callback, const std::string& id, const std::string& child);
    void candidateSpends(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void linkSpends(const HttpRequestPtr& req, Callback&& callback, const std::string& id);

private:
    static bool require_enabled(const Callback& callback);
};

}  // namespace Api
