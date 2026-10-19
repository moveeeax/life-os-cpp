/**
 * @file Mood.hpp
 * @brief The mood check-in of a day: a score from 1 to 5 and tags from a fixed
 *        set. Pure: the controller checks a body against these, the table
 *        (migration 050) holds one row per owner and local day.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace Habits::Mood {

inline constexpr int kScoreMin = 1;
inline constexpr int kScoreMax = 5;
/// The longest note of a check-in, in characters.
inline constexpr std::size_t kNoteMax = 500;

/// The tags a check-in may carry, in the order the page shows them.
inline constexpr std::array<std::string_view, 12> kTags = {
    "calm",
    "happy",
    "energetic",
    "focused",
    "grateful",
    "social",
    "tired",
    "stressed",
    "anxious",
    "sad",
    "irritable",
    "sick",
};

inline bool is_tag(std::string_view tag) {
    return std::find(kTags.begin(), kTags.end(), tag) != kTags.end();
}

/**
 * The tags of a body: an array of known tags, repeats dropped, kept in the
 * order of kTags so a check-in reads the same however it was sent. nullopt
 * when the value is not such an array.
 */
inline std::optional<std::vector<std::string>> tags_of(const nlohmann::json& value) {
    if (!value.is_array() || value.size() > kTags.size() * 2) {
        return std::nullopt;
    }
    std::vector<std::string> sent;
    for (const auto& t : value) {
        if (!t.is_string() || !is_tag(t.get<std::string>())) {
            return std::nullopt;
        }
        sent.push_back(t.get<std::string>());
    }
    std::vector<std::string> out;
    for (const auto tag : kTags) {
        if (std::find(sent.begin(), sent.end(), tag) != sent.end()) {
            out.emplace_back(tag);
        }
    }
    return out;
}

}  // namespace Habits::Mood
