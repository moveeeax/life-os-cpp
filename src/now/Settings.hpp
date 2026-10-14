/**
 * @file Settings.hpp
 * @brief The corner layout of Now: which chip sits in which corner, a corner
 *        possibly empty, every chip at most once.
 */

#pragma once

#include <array>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace Now::Settings {

inline constexpr std::array<std::string_view, 4> kCorners = {"tl", "tr", "bl", "br"};
inline constexpr std::array<std::string_view, 4> kChips = {"body", "goals", "day", "tasks"};

/// Body top left, Goals top right, Food and money bottom left, Tasks bottom right.
inline nlohmann::json default_corners() {
    return {{"tl", "body"}, {"tr", "goals"}, {"bl", "day"}, {"br", "tasks"}};
}

/// What is wrong with a layout, or nullopt: an object with exactly the four
/// corners, each a known chip or null, no chip twice.
inline std::optional<std::string> corners_problem(const nlohmann::json& corners) {
    if (!corners.is_object() || corners.size() != kCorners.size()) {
        return "corners must be an object with tl, tr, bl and br";
    }
    std::set<std::string> seen;
    for (const auto corner : kCorners) {
        const auto it = corners.find(std::string(corner));
        if (it == corners.end()) {
            return "corners must be an object with tl, tr, bl and br";
        }
        if (it->is_null()) {
            continue;
        }
        if (!it->is_string()) {
            return "a corner holds a chip name or null";
        }
        const std::string chip = it->get<std::string>();
        bool known = false;
        for (const auto k : kChips) {
            known = known || chip == k;
        }
        if (!known) {
            return "a corner holds body, goals, day, tasks or null";
        }
        if (!seen.insert(chip).second) {
            return "a chip sits in one corner at most";
        }
    }
    return std::nullopt;
}

}  // namespace Now::Settings
