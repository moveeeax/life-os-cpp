/**
 * @file GoalsController.hpp
 * @brief The /api/v1/goals routes: goals of four kinds with their progress
 *        and pace at the caller's date, check-ins, sections and milestones.
 *        Every handler checks the module switch, then the caller's user id;
 *        rows belong to that user only.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class GoalsController : public HttpController<GoalsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(GoalsController::listGoals, "/api/v1/goals", Get);
    ADD_METHOD_TO(GoalsController::createGoal, "/api/v1/goals", Post);
    ADD_METHOD_TO(GoalsController::getGoal, "/api/v1/goals/{id}", Get);
    ADD_METHOD_TO(GoalsController::updateGoal, "/api/v1/goals/{id}", Patch);
    ADD_METHOD_TO(GoalsController::deleteGoal, "/api/v1/goals/{id}", Delete);
    ADD_METHOD_TO(GoalsController::putCheckin, "/api/v1/goals/{id}/checkins", Post);
    ADD_METHOD_TO(GoalsController::deleteCheckin, "/api/v1/goals/{id}/checkins/{child}", Delete);
    ADD_METHOD_TO(GoalsController::addSection, "/api/v1/goals/{id}/sections", Post);
    ADD_METHOD_TO(GoalsController::updateSection, "/api/v1/goals/{id}/sections/{child}", Patch);
    ADD_METHOD_TO(GoalsController::deleteSection, "/api/v1/goals/{id}/sections/{child}", Delete);
    ADD_METHOD_TO(GoalsController::addMilestone, "/api/v1/goals/{id}/milestones", Post);
    ADD_METHOD_TO(GoalsController::deleteMilestone, "/api/v1/goals/{id}/milestones/{child}", Delete);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void listGoals(const HttpRequestPtr& req, Callback&& callback);
    void createGoal(const HttpRequestPtr& req, Callback&& callback);
    void getGoal(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void updateGoal(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteGoal(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void putCheckin(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteCheckin(const HttpRequestPtr& req, Callback&& callback, const std::string& id, const std::string& child);
    void addSection(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void updateSection(const HttpRequestPtr& req, Callback&& callback, const std::string& id, const std::string& child);
    void deleteSection(const HttpRequestPtr& req, Callback&& callback, const std::string& id, const std::string& child);
    void addMilestone(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteMilestone(const HttpRequestPtr& req,
                         Callback&& callback,
                         const std::string& id,
                         const std::string& child);

private:
    static bool require_enabled(const Callback& callback);
};

}  // namespace Api
