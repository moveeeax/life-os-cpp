/**
 * @file WorkoutController.hpp
 * @brief Workout module routes: the exercise library and routines.
 *
 * Every route needs the module switch (Core::workout_enabled()), the
 * fitness:read permission and a user account: rows belong to the caller.
 *
 * Declarations only — the handler bodies live in WorkoutController.cpp
 * (compiled once into app_core; docs/ARCHITECTURE.md §4). The route
 * macros (ADD_METHOD_TO) must stay in this header: Drogon's METHOD_LIST
 * registration is part of the class definition, and
 * scripts/check-routes-registered.sh greps the src/api headers for them.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class WorkoutController : public HttpController<WorkoutController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(WorkoutController::listExercises, "/api/v1/workout/exercises", Get);
    ADD_METHOD_TO(WorkoutController::createExercise, "/api/v1/workout/exercises", Post);
    ADD_METHOD_TO(WorkoutController::getExercise, "/api/v1/workout/exercises/{id}", Get);
    ADD_METHOD_TO(WorkoutController::updateExercise, "/api/v1/workout/exercises/{id}", Patch);
    ADD_METHOD_TO(WorkoutController::listRoutines, "/api/v1/workout/routines", Get);
    ADD_METHOD_TO(WorkoutController::getRoutine, "/api/v1/workout/routines/{id}", Get);
    ADD_METHOD_TO(WorkoutController::putRoutine, "/api/v1/workout/routines/{id}", Put);
    ADD_METHOD_TO(WorkoutController::deleteRoutine, "/api/v1/workout/routines/{id}", Delete);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void listExercises(const HttpRequestPtr& req, Callback&& callback);
    void createExercise(const HttpRequestPtr& req, Callback&& callback);
    void getExercise(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void updateExercise(const HttpRequestPtr& req, Callback&& callback, const std::string& id);

    void listRoutines(const HttpRequestPtr& req, Callback&& callback);
    void getRoutine(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void putRoutine(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteRoutine(const HttpRequestPtr& req, Callback&& callback, const std::string& id);

private:
    /// 404 while the module is off; the routes stay registered.
    static bool require_enabled(const Callback& callback);
};

}  // namespace Api
