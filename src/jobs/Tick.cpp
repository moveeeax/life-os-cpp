/**
 * @file Tick.cpp
 * @brief Body for src/jobs/Tick.hpp.
 */

#include "jobs/Tick.hpp"

#include <chrono>
#include <exception>

#include <spdlog/spdlog.h>

#include "cache/Cache.hpp"
#include "jobs/Jobs.hpp"

namespace Jobs::Tick {

std::string marker_key(const std::string& type) {
    return "jobs:tick:" + type;
}

bool submit(const std::string& type, const nlohmann::json& payload, long ttl_seconds) {
    const std::string key = marker_key(type);
    if (Cache::is_initialized() && !Cache::get().set_nx(key, "1", std::chrono::seconds(ttl_seconds))) {
        spdlog::debug("tick {}: the last one is still queued or running", type);
        return false;
    }
    try {
        get().submit(type, payload);
    } catch (const std::exception&) {
        if (Cache::is_initialized()) {
            Cache::get().del(key);
        }
        throw;
    }
    return true;
}

void done(const std::string& type) {
    if (!Cache::is_initialized()) {
        return;
    }
    try {
        Cache::get().del(marker_key(type));
    } catch (const std::exception& e) {
        spdlog::warn("tick {}: the marker was not cleared (it expires): {}", type, e.what());
    }
}

}  // namespace Jobs::Tick
