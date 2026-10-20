/**
 * @file Digest.cpp
 * @brief Body for src/goals/now/Digest.hpp — compiled once into app_core.
 */

#include "goals/now/Digest.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "goals/Progress.hpp"
#include "now/Today.hpp"
#include "repositories/goals/GoalRepository.hpp"
#include "repositories/goals/Sources.hpp"

namespace Goals {

namespace {

using json = nlohmann::json;
using Repositories::Goals::GoalRepository;
using Repositories::Goals::Sources;

json goals_with_progress(const std::string& owner,
                         const std::string& date,
                         const std::string& tz,
                         std::uint32_t permissions) {
    GoalRepository repo;
    Sources sources(owner, tz, permissions);
    const auto counts = repo.task_counts(owner);
    const auto points = repo.checkin_points_all(owner);
    json out = json::array();
    for (auto row : repo.list(owner, "active")) {
        const std::string id = row["id"].get<std::string>();
        const auto goal = GoalRepository::progress_goal(row);
        const auto c = counts.find(id);
        const auto p = points.find(id);
        Sources::Read read;
        if (goal.kind == "number") {
            read = sources.points(row, date, p == points.end() ? std::vector<Progress::Checkin>{} : p->second);
        }
        row["progress"] = Progress::compute(
            goal, date, Progress::values_of(read.points), c == counts.end() ? Progress::Counts{0, 0} : c->second);
        out.push_back(std::move(row));
    }
    return out;
}

}  // namespace

json digest(const std::string& owner, const std::string& day, const std::string& tz, std::uint32_t permissions) {
    return ::Now::Today::goals_block(goals_with_progress(owner, day, tz, permissions));
}

}  // namespace Goals
