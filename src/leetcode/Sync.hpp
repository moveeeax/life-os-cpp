/**
 * @file Sync.hpp
 * @brief LeetCode's public profile into Life OS: the solved counts of a
 *        username from leetcode.com/graphql (no key; the same query the
 *        profile page makes), stored as today's snapshot. The job
 *        leetcode_sync pulls every profile; a failed pull keeps the error and
 *        stores nothing, so a day is skipped, not counted as zero.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace LeetCode {

inline constexpr const char* kSyncJob = "leetcode_sync";
inline constexpr const char* kGraphqlUrl = "https://leetcode.com/graphql";

struct Stats {
    int total = 0;
    int easy = 0;
    int medium = 0;
    int hard = 0;
};

/// A LeetCode username: 1-30 of letters, digits, _ . -
bool valid_username(std::string_view u);

/// The solved counts in a graphql answer; nullopt for an unknown user or junk.
std::optional<Stats> parse_stats(const std::string& body);

/// Pulls one username (Net::Http::transport()); nullopt with @p why on failure.
std::optional<Stats> fetch(const std::string& username, std::string& why);

/// The job: every profile pulled once; with {"owner": id} in @p payload only that user's.
nlohmann::json process_sync(const nlohmann::json& payload);

}  // namespace LeetCode
