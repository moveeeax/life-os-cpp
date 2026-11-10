/**
 * @file FitnessGoalsController.cpp
 * @brief Bodies for src/api/FitnessGoalsController.hpp: read the steps goal,
 *        and set it (a whole number from 1000 to 100000) or clear it back to
 *        the default with null.
 */

#include "api/FitnessGoalsController.hpp"

#include <optional>
#include <string>

#include <drogon/drogon.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/Validation.hpp"
#include "repositories/fitness/GoalsRepository.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Repositories::Fitness::GoalsRepository;

}  // namespace

void FitnessGoalsController::get(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::fitness_enabled(), "fitness", req, callback, owner);
    with_repo_errors(callback, "fitness.goals.get", [&] {
        callback(Response::ok(json{{"data", GoalsRepository().get(owner)}}));
    });
}

void FitnessGoalsController::put(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::fitness_enabled(), "fitness", req, callback, owner);
    json body;
    if (!Validation::parse_body(req, body, callback)) {
        return;
    }
    const auto steps = body.find("steps");
    std::optional<int> value;
    if (steps == body.end() ||
        (!steps->is_null() && (!steps->is_number_integer() || steps->get<long long>() < GoalsRepository::kMinSteps ||
                               steps->get<long long>() > GoalsRepository::kMaxSteps))) {
        callback(ErrorResponse::bad_request("invalid_steps",
                                            "steps is a whole number from 1000 to 100000, or null for the default"));
        return;
    }
    if (!steps->is_null()) {
        value = static_cast<int>(steps->get<long long>());
    }
    with_repo_errors(callback, "fitness.goals.put", [&] {
        GoalsRepository repo;
        repo.set_steps(owner, value);
        callback(Response::ok(json{{"data", repo.get(owner)}}));
    });
}

}  // namespace Api
