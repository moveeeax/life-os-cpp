/**
 * @file Digest.cpp
 * @brief Body for src/goals/now/Digest.hpp — compiled once into app_core.
 */

#include "goals/now/Digest.hpp"

#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "goals/Progress.hpp"
#include "now/Today.hpp"
#include "repositories/goals/GoalRepository.hpp"

namespace Goals {

namespace {

using json = nlohmann::json;
using Repositories::Goals::GoalRepository;

json goals_with_progress(const std::string& owner, const std::string& date) {
    GoalRepository repo;
    const auto counts = repo.task_counts(owner);
    const auto points = repo.checkin_points_all(owner);
    json out = json::array();
    for (auto row : repo.list(owner, "active")) {
        const std::string id = row["id"].get<std::string>();
        const auto goal = GoalRepository::progress_goal(row);
        const auto c = counts.find(id);
        const auto p = points.find(id);
        row["progress"] =
            Progress::compute(goal,
                              date,
                              p == points.end() || goal.kind != "number" ? std::vector<Progress::Checkin>{} : p->second,
                              c == counts.end() ? Progress::Counts{0, 0} : c->second);
        out.push_back(std::move(row));
    }
    return out;
}

}  // namespace

json digest(const std::string& owner, const std::string& day) {
    return ::Now::Today::goals_block(goals_with_progress(owner, day));
}

}  // namespace Goals
