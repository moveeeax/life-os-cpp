/**
 * @file HabitsController.hpp
 * @brief /api/v1/habits: the owner's habits, their manual ticks and the days
 *        evaluated with the values from Mi Fitness and Training
 *        (docs/superpowers/specs/2026-10-14-habits-design.md).
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class HabitsController : public HttpController<HabitsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(HabitsController::list, "/api/v1/habits", Get);
    ADD_METHOD_TO(HabitsController::create, "/api/v1/habits", Post);
    ADD_METHOD_TO(HabitsController::days, "/api/v1/habits/days", Get);
    ADD_METHOD_TO(HabitsController::update, "/api/v1/habits/{id}", Patch);
    ADD_METHOD_TO(HabitsController::remove, "/api/v1/habits/{id}", Delete);
    ADD_METHOD_TO(HabitsController::archive, "/api/v1/habits/{id}/archive", Post);
    ADD_METHOD_TO(HabitsController::putCheck, "/api/v1/habits/{id}/checks/{date}", Put);
    ADD_METHOD_TO(HabitsController::deleteCheck, "/api/v1/habits/{id}/checks/{date}", Delete);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void list(const HttpRequestPtr& req, Callback&& callback);
    void create(const HttpRequestPtr& req, Callback&& callback);
    void days(const HttpRequestPtr& req, Callback&& callback);
    void update(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void remove(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void archive(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void putCheck(const HttpRequestPtr& req, Callback&& callback, const std::string& id, const std::string& date);
    void deleteCheck(const HttpRequestPtr& req, Callback&& callback, const std::string& id, const std::string& date);
};

}  // namespace Api
