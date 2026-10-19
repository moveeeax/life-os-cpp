/**
 * @file DigestCache.hpp
 * @brief The keys of the cached money part of GET /today (money/now/Digest).
 *
 * The digest runs about fifteen queries (the month report with its three
 * earlier periods, the day's spends, the net worth in USD), and the page
 * fetches /today again after every money write. It is cached in Redis per
 * owner and day under two version tokens: the owner's, replaced by every
 * money write of that owner (MoneyController), and the rates', replaced when
 * the money_rates job stores a day of quotes (the net worth reads them). A
 * new token makes every older entry unreachable; the TTL bounds what a lost
 * replacement (Redis away during the write) can leave stale.
 *
 * Fail-open: with Redis down the version reads come back empty, the cache
 * read misses and the store fails, so the digest is computed as before.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <random>
#include <string>

#include <spdlog/spdlog.h>

#include "cache/Cache.hpp"

namespace Money::DigestCache {

/// Seconds a cached digest lives at most.
inline constexpr long kTtlSec = 300;

inline constexpr const char* kRatesVersionKey = "money:digest:ver:rates";

inline std::string owner_version_key(const std::string& owner) {
    return "money:digest:ver:" + owner;
}

/// A token no earlier replacement used: the clock, a process-random prefix
/// and a counter, so a version key that was evicted and set again never
/// matches an entry stored under an older token.
inline std::string fresh_token() {
    static const unsigned long long prefix = std::random_device{}();
    static std::atomic<unsigned long long> counter{0};
    const auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return std::to_string(prefix) + "-" + std::to_string(now) + "-" + std::to_string(++counter);
}

/// Replaces the token under @p key; a Redis error is logged, never thrown.
inline void replace(const std::string& key) {
    if (!Cache::is_initialized()) {
        return;
    }
    try {
        Cache::get().set(key, fresh_token());
    } catch (const std::exception& e) {
        spdlog::warn("money digest cache: could not replace {}: {}", key, e.what());
    }
}

/// After a money write of @p owner has committed.
inline void touch_owner(const std::string& owner) {
    replace(owner_version_key(owner));
}

/// After the money_rates job stored a day of quotes.
inline void touch_rates() {
    replace(kRatesVersionKey);
}

/// The cache key of @p owner's digest on @p day under the current tokens;
/// "0" stands for a token that is not there (never written, or Redis away).
inline std::string entry_key(const std::string& owner, const std::string& day) {
    std::string owner_version = "0";
    std::string rates_version = "0";
    if (Cache::is_initialized()) {
        try {
            owner_version = Cache::get().get(owner_version_key(owner)).value_or("0");
            rates_version = Cache::get().get(kRatesVersionKey).value_or("0");
        } catch (const std::exception& e) {
            spdlog::debug("money digest cache: version read failed: {}", e.what());
        }
    }
    return "money:digest:" + owner + ":" + day + ":" + owner_version + ":" + rates_version;
}

}  // namespace Money::DigestCache
