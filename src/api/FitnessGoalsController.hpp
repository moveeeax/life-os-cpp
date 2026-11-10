/**
 * @file FitnessGoalsController.hpp
 * @brief /api/v1/fitness/goals: the caller's own fitness goals, for now the
 *        daily steps goal (8000 when unset).
 *
 * The routes sit under the fitness module like the measurements by hand: the
 * module switch (404 `fitness`), then a user account (403 no_user_account).
 * No permission bit and no linked band: the goal is the caller's own setting,
 * not data of the Mi account.
 *
 * Declarations only; the bodies live in FitnessGoalsController.cpp. The route
 * macros stay here for scripts/check-routes-registered.sh.
 */

#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class FitnessGoalsController : public HttpController<FitnessGoalsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(FitnessGoalsController::get, "/api/v1/fitness/goals", Get);
    ADD_METHOD_TO(FitnessGoalsController::put, "/api/v1/fitness/goals", Put);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void get(const HttpRequestPtr& req, Callback&& callback);
    void put(const HttpRequestPtr& req, Callback&& callback);
};

}  // namespace Api
