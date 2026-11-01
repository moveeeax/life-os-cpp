/**
 * @file CalendarCache.cpp
 * @brief Body for src/api/CalendarCache.hpp.
 */

#include "api/CalendarCache.hpp"

#include <exception>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "cache/Cache.hpp"

namespace Api::CalendarCache {

namespace {

using json = nlohmann::json;

/// The generation key lives a day past the last change; ranges live a minute, so a restart at 0
/// never meets an entry of the old count.
constexpr long kGenerationSeconds = 86400;

std::string generation_key(const std::string& owner) {
    return "calendar:gen:" + owner;
}

std::string down_key(const std::string& owner) {
    return "calendar:down:" + owner;
}

std::string range_key(const std::string& owner, long long from, long long to) {
    const auto gen = Cache::get().get(generation_key(owner));
    return "calendar:events:" + owner + ":" + gen.value_or("0") + ":" + std::to_string(from) + ":" + std::to_string(to);
}

json to_json(const Calendar::Event& e) {
    return json{{"id", e.id},
                {"title", e.title},
                {"start", e.start},
                {"end", e.end},
                {"all_day", e.all_day},
                {"free", e.free},
                {"location", e.location},
                {"start_text", e.start_text},
                {"end_text", e.end_text}};
}

Calendar::Event from_json(const json& j) {
    Calendar::Event e;
    e.id = j.at("id").get<std::string>();
    e.title = j.value("title", std::string());
    e.start = j.at("start").get<long long>();
    e.end = j.at("end").get<long long>();
    e.all_day = j.value("all_day", false);
    e.free = j.value("free", false);
    e.location = j.value("location", std::string());
    e.start_text = j.value("start_text", std::string());
    e.end_text = j.value("end_text", std::string());
    return e;
}

}  // namespace

std::optional<std::vector<Calendar::Event>> get(const std::string& owner, long long from, long long to) {
    if (!Cache::is_initialized()) {
        return std::nullopt;
    }
    try {
        const auto hit = Cache::get().get(range_key(owner, from, to));
        if (!hit) {
            return std::nullopt;
        }
        std::vector<Calendar::Event> out;
        for (const auto& item : json::parse(*hit)) {
            out.push_back(from_json(item));
        }
        return out;
    } catch (const std::exception& e) {
        spdlog::debug("calendar cache: an entry is not usable: {}", e.what());
        return std::nullopt;
    }
}

void put(const std::string& owner, long long from, long long to, const std::vector<Calendar::Event>& events) {
    if (!Cache::is_initialized()) {
        return;
    }
    try {
        json list = json::array();
        for (const auto& e : events) {
            list.push_back(to_json(e));
        }
        Cache::get().set(range_key(owner, from, to), list.dump(), kTtlSeconds);
    } catch (const std::exception& e) {
        spdlog::debug("calendar cache: not stored: {}", e.what());
    }
}

void forget(const std::string& owner) {
    if (!Cache::is_initialized()) {
        return;
    }
    try {
        Cache::get().incr(generation_key(owner));
        Cache::get().expire(generation_key(owner), kGenerationSeconds);
        Cache::get().del(down_key(owner));
    } catch (const std::exception& e) {
        // The old entries live out their minute.
        spdlog::warn("calendar cache: the user's events were not forgotten: {}", e.what());
    }
}

bool down(const std::string& owner) {
    if (!Cache::is_initialized()) {
        return false;
    }
    try {
        return Cache::get().exists(down_key(owner));
    } catch (const std::exception&) {
        return false;
    }
}

void mark_down(const std::string& owner) {
    if (!Cache::is_initialized()) {
        return;
    }
    try {
        Cache::get().set(down_key(owner), "1", kDownSeconds);
    } catch (const std::exception& e) {
        spdlog::debug("calendar cache: the down key was not set: {}", e.what());
    }
}

}  // namespace Api::CalendarCache
