/**
 * @file Fields.hpp
 * @brief The fixed lists and limits of a task: six areas (work is not one of
 *        them: the job's tasks live in Linear), three efforts, text lengths.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace Tasks::Fields {

inline constexpr std::array<std::string_view, 6> kAreas{
    "finance", "health", "travel", "growth", "relationships", "projects"};
inline constexpr std::array<std::string_view, 3> kEfforts{"5min", "30min", "deep"};

inline constexpr std::size_t kTitleMax = 200;
inline constexpr std::size_t kNextStepMax = 300;
inline constexpr std::size_t kNoteMax = 2000;
inline constexpr std::size_t kNoteTextMax = 1000;
inline constexpr std::size_t kPhraseMax = 2000;

inline bool is_area(std::string_view v) {
    return std::find(kAreas.begin(), kAreas.end(), v) != kAreas.end();
}

inline bool is_effort(std::string_view v) {
    return std::find(kEfforts.begin(), kEfforts.end(), v) != kEfforts.end();
}

}  // namespace Tasks::Fields
