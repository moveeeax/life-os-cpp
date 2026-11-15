/**
 * @file LinkChecks.hpp
 * @brief The ownership half of a link field (deal_id, project_id, trip_id,
 *        task_id): the row it names must be the caller's. A link to a module
 *        that is switched off reads as not yours. The caller checks the uuid
 *        shape first and calls these only when the body has no errors yet.
 */

#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

#include "api/Validation.hpp"
#include "repositories/practice/DealRepository.hpp"
#include "repositories/tasks/ProjectRepository.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "repositories/travel/TripRepository.hpp"
#include "utils/Modules.hpp"

namespace Api::Links {

/// Adds `<field> not_yours` with @p message when body[field] is a string that
/// @p owns refuses. Absent, null or another type is left to the shape checks.
inline void owned(Validation::Errors& errs,
                  const nlohmann::json& body,
                  const std::string& field,
                  const std::string& message,
                  const std::function<bool(const std::string&)>& owns) {
    const auto it = body.find(field);
    if (it == body.end() || !it->is_string()) {
        return;
    }
    if (!owns(it->get<std::string>())) {
        errs.add(field, "not_yours", message);
    }
}

/// A deal of the caller's practice.
inline void deal(Validation::Errors& errs, const nlohmann::json& body, const std::string& owner) {
    owned(errs, body, "deal_id", "the deal is not yours", [&](const std::string& id) {
        return Core::practice_enabled() && Repositories::Practice::DealRepository().client_of(owner, id).has_value();
    });
}

/// A project or course of the caller.
inline void project(Validation::Errors& errs, const nlohmann::json& body, const std::string& owner) {
    owned(errs, body, "project_id", "the project is not yours", [&](const std::string& id) {
        return Repositories::Tasks::ProjectRepository().owns(owner, id);
    });
}

/// A task of the caller.
inline void task(Validation::Errors& errs, const nlohmann::json& body, const std::string& owner) {
    owned(errs, body, "task_id", "the task is not yours", [&](const std::string& id) {
        return Repositories::Tasks::TaskRepository().get(owner, id).has_value();
    });
}

/// A trip of the caller.
inline void trip(Validation::Errors& errs, const nlohmann::json& body, const std::string& owner) {
    owned(errs, body, "trip_id", "the trip is not yours", [&](const std::string& id) {
        return Core::travel_enabled() && Repositories::Travel::TripRepository().get(owner, id).has_value();
    });
}

}  // namespace Api::Links
