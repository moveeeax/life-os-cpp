/**
 * @file FoodController.hpp
 * @brief Food module routes: products, the diary, goals, Open Food Facts.
 *
 * Every route needs the module switch (Core::food_enabled()) and a user
 * account: rows belong to the caller. No permission bit: Food has no
 * dependency on the band, every confirmed user has it.
 *
 * Declarations only; the bodies live in FoodController.cpp. The route macros
 * stay in this header: scripts/check-routes-registered.sh greps them.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class FoodController : public HttpController<FoodController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(FoodController::getGoals, "/api/v1/food/goals", Get);
    ADD_METHOD_TO(FoodController::putGoals, "/api/v1/food/goals", Put);
    ADD_METHOD_TO(FoodController::listItems, "/api/v1/food/items", Get);
    ADD_METHOD_TO(FoodController::createItem, "/api/v1/food/items", Post);
    ADD_METHOD_TO(FoodController::itemFromOff, "/api/v1/food/items/from-off", Post);
    ADD_METHOD_TO(FoodController::updateItem, "/api/v1/food/items/{id}", Patch);
    ADD_METHOD_TO(FoodController::deleteItem, "/api/v1/food/items/{id}", Delete);
    ADD_METHOD_TO(FoodController::recentItems, "/api/v1/food/recent", Get);
    ADD_METHOD_TO(FoodController::offSearch, "/api/v1/food/off/search", Get);
    ADD_METHOD_TO(FoodController::day, "/api/v1/food/day", Get);
    ADD_METHOD_TO(FoodController::week, "/api/v1/food/week", Get);
    ADD_METHOD_TO(FoodController::createEntry, "/api/v1/food/entries", Post);
    ADD_METHOD_TO(FoodController::createEntries, "/api/v1/food/entries/batch", Post);
    ADD_METHOD_TO(FoodController::updateEntry, "/api/v1/food/entries/{id}", Patch);
    ADD_METHOD_TO(FoodController::deleteEntry, "/api/v1/food/entries/{id}", Delete);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void getGoals(const HttpRequestPtr& req, Callback&& callback);
    void putGoals(const HttpRequestPtr& req, Callback&& callback);

    void listItems(const HttpRequestPtr& req, Callback&& callback);
    void createItem(const HttpRequestPtr& req, Callback&& callback);
    void itemFromOff(const HttpRequestPtr& req, Callback&& callback);
    void updateItem(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteItem(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void recentItems(const HttpRequestPtr& req, Callback&& callback);
    void offSearch(const HttpRequestPtr& req, Callback&& callback);

    void day(const HttpRequestPtr& req, Callback&& callback);
    void week(const HttpRequestPtr& req, Callback&& callback);
    void createEntry(const HttpRequestPtr& req, Callback&& callback);
    void createEntries(const HttpRequestPtr& req, Callback&& callback);
    void updateEntry(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteEntry(const HttpRequestPtr& req, Callback&& callback, const std::string& id);

private:
    /// 404 while the module is off; the routes stay registered.
    static bool require_enabled(const Callback& callback);
};

}  // namespace Api
