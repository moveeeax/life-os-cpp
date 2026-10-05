/**
 * @file WorkoutController.hpp
 * @brief Workout module routes: the exercise library, routines, logged
 *        sessions with their sets, and the links to Mi Fitness data.
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
    ADD_METHOD_TO(WorkoutController::startSession, "/api/v1/workout/sessions", Post);
    ADD_METHOD_TO(WorkoutController::listSessions, "/api/v1/workout/sessions", Get);
    ADD_METHOD_TO(WorkoutController::activeSession, "/api/v1/workout/sessions/active", Get);
    ADD_METHOD_TO(WorkoutController::getSession, "/api/v1/workout/sessions/{id}", Get);
    ADD_METHOD_TO(WorkoutController::patchSession, "/api/v1/workout/sessions/{id}", Patch);
    ADD_METHOD_TO(WorkoutController::deleteSession, "/api/v1/workout/sessions/{id}", Delete);
    ADD_METHOD_TO(WorkoutController::addSessionExercise, "/api/v1/workout/sessions/{id}/exercises", Post);
    ADD_METHOD_TO(WorkoutController::removeSessionExercise, "/api/v1/workout/sessions/{id}/exercises/{eid}", Delete);
    ADD_METHOD_TO(WorkoutController::sessionHeartRate, "/api/v1/workout/sessions/{id}/heart-rate", Get);
    ADD_METHOD_TO(WorkoutController::putSet, "/api/v1/workout/sets/{id}", Put);
    ADD_METHOD_TO(WorkoutController::deleteSet, "/api/v1/workout/sets/{id}", Delete);
    ADD_METHOD_TO(WorkoutController::readiness, "/api/v1/workout/readiness", Get);
    ADD_METHOD_TO(WorkoutController::reconcile, "/api/v1/workout/reconcile", Post);
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

    void startSession(const HttpRequestPtr& req, Callback&& callback);
    void listSessions(const HttpRequestPtr& req, Callback&& callback);
    void activeSession(const HttpRequestPtr& req, Callback&& callback);
    void getSession(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void patchSession(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteSession(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void addSessionExercise(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void removeSessionExercise(const HttpRequestPtr& req,
                               Callback&& callback,
                               const std::string& id,
                               const std::string& eid);
    void sessionHeartRate(const HttpRequestPtr& req, Callback&& callback, const std::string& id);

    void putSet(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteSet(const HttpRequestPtr& req, Callback&& callback, const std::string& id);

    void readiness(const HttpRequestPtr& req, Callback&& callback);
    /// Needs fitness:sync on top of the common guards.
    void reconcile(const HttpRequestPtr& req, Callback&& callback);

private:
    /// 404 while the module is off; the routes stay registered.
    static bool require_enabled(const Callback& callback);
};

}  // namespace Api
