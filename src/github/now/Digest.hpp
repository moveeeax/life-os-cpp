/**
 * @file Digest.hpp
 * @brief The github part of the day's digest (GET /api/v1/today): how many
 *        repositories need attention and the first two by last push, the
 *        one line of the Sunday weekly review. One query; nothing when the
 *        count is zero, which covers a user who never linked GitHub (the
 *        rows go with the link). The Now screen (api/TodayController.cpp)
 *        reads each module through one such function and composes nothing
 *        itself.
 */

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "repositories/github/RepoRepository.hpp"

namespace Github {

/// How many full names the review line shows.
inline constexpr int kReviewTop = 2;

/// {attention: N, top: [full names]} of @p owner's repositories that need attention; null when none.
inline nlohmann::json digest(const std::string& owner) {
    const nlohmann::json summary = Repositories::Github::RepoRepository().attention_summary(owner, kReviewTop);
    if (summary.value("count", 0) <= 0) {
        return nullptr;
    }
    return nlohmann::json{{"attention", summary["count"]}, {"top", summary["top"]}};
}

}  // namespace Github
