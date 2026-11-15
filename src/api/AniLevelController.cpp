/**
 * @file AniLevelController.cpp
 * @brief Body for src/api/AniLevelController.hpp.
 */

#include "api/AniLevelController.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include <drogon/drogon.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "domain/AniLevel.hpp"
#include "repositories/Zones.hpp"
#include "repositories/agent/LevelRepository.hpp"
#include "repositories/fitness/GoalsRepository.hpp"
#include "repositories/food/GoalsBody.hpp"
#include "repositories/food/WaterRepository.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
namespace Level = ::Domain::AniLevel;

/// The goals the rules measure against, as they stand today.
Level::Targets targets_of(const std::string& owner, const std::string& today) {
    Level::Targets t;
    t.steps = Repositories::Fitness::GoalsRepository().steps(owner);
    t.water_ml = Repositories::WaterRepository().day(owner, today)["goal_ml"].get<int>();
    const json food = Repositories::food_goals_body(owner, today)["targets"];
    if (food.is_object()) {
        if (food.contains("kcal") && food["kcal"].is_number()) {
            t.kcal = food["kcal"].get<int>();
        }
        if (food.contains("protein_g") && food["protein_g"].is_number()) {
            t.protein_g = food["protein_g"].get<int>();
        }
    }
    t.monthly_budget = Repositories::Agent::LevelRepository().monthly_budgets(owner);
    return t;
}

}  // namespace

void AniLevelController::get(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    std::string tz = req->getParameter("tz");
    if (tz.empty()) {
        tz = "UTC";
    }
    if (!Repositories::Zones::is_zone(tz)) {
        callback(ErrorResponse::bad_request("invalid_query", "tz must be an IANA zone like Asia/Almaty"));
        return;
    }
    with_repo_errors(callback, "ani.level", [&] {
        Repositories::Agent::LevelRepository repo;
        const std::string today = Repositories::Zones::today_in(tz).value_or("");
        const std::string through = repo.last_closed_day(tz);
        const std::string started = repo.started_on(owner, tz);
        const auto days = started <= through ? repo.days(owner, tz, started, through) : std::vector<Level::Day>{};
        const Level::Result r = Level::score(days, targets_of(owner, today));

        json scales = json::array();
        int weakest = 0;
        for (int s = 0; s < Level::kScales; ++s) {
            const auto& sc = r.scales[s];
            const int level = Level::level_of(sc.points);
            scales.push_back(json{{"id", Level::kScaleIds[s]},
                                  {"level", level},
                                  {"points", sc.points},
                                  {"from", Level::threshold(level)},
                                  {"to", Level::threshold(level + 1)},
                                  {"week", sc.week},
                                  {"debt", sc.debt}});
            if (sc.points < r.scales[weakest].points) {
                weakest = s;
            }
        }
        callback(Response::ok(json{{"data",
                                    {{"level", Level::overall(r)},
                                     {"weakest", Level::kScaleIds[weakest]},
                                     {"started_on", started},
                                     {"through", through},
                                     {"scales", scales}}}}));
    });
}

}  // namespace Api
