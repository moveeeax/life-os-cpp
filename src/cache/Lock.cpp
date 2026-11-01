/**
 * @file Lock.cpp
 * @brief Body for src/cache/Lock.hpp.
 */

#include "cache/Lock.hpp"

#include <exception>
#include <utility>

#include <spdlog/spdlog.h>

#include "cache/Cache.hpp"
#include "utils/Crypto.hpp"

namespace Cache {

Lock::Lock(std::string key, std::chrono::milliseconds ttl, bool held_without_cache) : key_(std::move(key)) {
    if (!is_initialized()) {
        held_ = held_without_cache;
        return;
    }
    try {
        token_ = Utils::Crypto::random_hex(16);
        held_ = get().set_nx(key_, token_, ttl);
        in_cache_ = held_;
    } catch (const std::exception& e) {
        spdlog::warn("lock {}: not taken: {}", key_, e.what());
        held_ = false;
    }
}

Lock::~Lock() {
    release();
}

void Lock::release() {
    if (!held_) {
        return;
    }
    held_ = false;
    if (!in_cache_ || !is_initialized()) {
        return;
    }
    try {
        if (!get().del_if_equals(key_, token_)) {
            spdlog::info("lock {}: expired before its release; the next holder keeps it", key_);
        }
    } catch (const std::exception& e) {
        spdlog::warn("lock {}: not released: {}", key_, e.what());
    }
}

}  // namespace Cache
