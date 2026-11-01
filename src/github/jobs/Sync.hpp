/**
 * @file Sync.hpp
 * @brief The worker job github_sync. With {owner} it syncs one user: every
 *        repository GitHub gives (github/Client.hpp), the verdict of the
 *        attention rules after the user's mutes, RepoRepository::replace in
 *        one transaction, mark_synced with the org hint. A failure marks the
 *        link (mark_failed, the code of the Failure) and keeps the old list.
 *        One job per owner runs at a time (a Redis lock). Without an owner
 *        it is the scheduler's tick: one {owner} job queued per user whose
 *        sync is due (Github::kSyncEveryHours).
 */

#pragma once

#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "github/Client.hpp"
#include "repositories/github/RepoRepository.hpp"

namespace Github {

/// The Redis key that says a sync of @p owner runs now (fifteen minutes at most).
std::string lock_key(const std::string& owner);
inline constexpr int kLockSeconds = 15 * 60;

/// The row of one repository with its verdict (Github::attention over facts_of, without @p muted) at @p now.
Repositories::Github::RepoRepository::Input input_of(const Repo& r, const std::set<std::string>& muted, long long now);

/// One user's sync; the outcome as the job's result ({status: done | failed | skipped, ...}).
nlohmann::json sync_one(const std::string& owner);

/// The job: {owner} runs sync_one; {} queues one {owner} job per due user.
nlohmann::json process_sync(const nlohmann::json& payload);

}  // namespace Github
