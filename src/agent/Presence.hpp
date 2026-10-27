/**
 * @file Presence.hpp
 * @brief Whether the user's Now page is open: while it is, the page asks for
 *        Ani's inbox every few seconds (api/AniHookController claim and
 *        count), and each ask is remembered in Redis (ani:seen:<user>, unix
 *        seconds, kept an hour). Without Redis nothing is known. A browser
 *        slows the timers of a tab in the background, so such a page may
 *        read as away.
 */

#pragma once

#include <chrono>
#include <exception>
#include <optional>
#include <string>

#include <spdlog/spdlog.h>

#include "cache/Cache.hpp"

namespace Agent::Presence {

/// A page asked within this many seconds is open.
inline constexpr int kOpenSeconds = 20;
inline constexpr long kKeepSeconds = 3600;

inline std::string key_of(const std::string& owner) {
    return "ani:seen:" + owner;
}

inline long long now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// Remembers that the user's page asked just now.
inline void mark(const std::string& owner) {
    if (!Cache::is_initialized()) {
        return;
    }
    try {
        Cache::get().set(key_of(owner), std::to_string(now_seconds()), kKeepSeconds);
    } catch (const std::exception& e) {
        spdlog::warn("ani.presence: mark failed: {}", e.what());
    }
}

/// Seconds since the page last asked; nullopt when unknown.
inline std::optional<long long> seen_ago(const std::string& owner) {
    if (!Cache::is_initialized()) {
        return std::nullopt;
    }
    try {
        const auto v = Cache::get().get(key_of(owner));
        if (!v) {
            return std::nullopt;
        }
        const long long ago = now_seconds() - std::stoll(*v);
        return ago < 0 ? 0 : ago;
    } catch (const std::exception& e) {
        spdlog::warn("ani.presence: read failed: {}", e.what());
        return std::nullopt;
    }
}

}  // namespace Agent::Presence
