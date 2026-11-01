/**
 * @file Lock.hpp
 * @brief A Redis lock held by one holder: SET NX with a random token of its
 *        own and a TTL, released with a compare-and-delete, so a holder that
 *        outlived its TTL never deletes the lock the next holder took. Used by
 *        the jobs that must not run twice at once (an agent's start and stop,
 *        a user's webhook events, the Telegram poll).
 */

#pragma once

#include <chrono>
#include <string>

namespace Cache {

class Lock {
public:
    /// Tries to take @p key for @p ttl. Without a cache the lock counts as
    /// held when @p held_without_cache (a single process has no one to race).
    /// A Redis error counts as not held (see CacheManager::set_nx).
    Lock(std::string key, std::chrono::milliseconds ttl, bool held_without_cache = true);
    ~Lock();

    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

    bool held() const { return held_; }

    /// Gives the lock up now (also done by the destructor); a no-op when not
    /// held or released already, and when another holder has it by now.
    void release();

    const std::string& key() const { return key_; }

private:
    std::string key_;
    std::string token_;
    bool held_ = false;
    bool in_cache_ = false;
};

}  // namespace Cache
