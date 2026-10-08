/**
 * @file Fields.hpp
 * @brief The fixed lists and limits of a goal: four kinds, three statuses,
 *        text lengths. Areas are the six of Tasks (Tasks::Fields::kAreas).
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace Goals::Fields {

inline constexpr std::array<std::string_view, 4> kKinds{"number", "steps", "count", "binary"};
inline constexpr std::array<std::string_view, 3> kStatuses{"active", "done", "dropped"};

inline constexpr std::size_t kTitleMax = 200;
inline constexpr std::size_t kWhyMax = 300;
inline constexpr std::size_t kUnitMax = 20;
inline constexpr std::size_t kNameMax = 120;
inline constexpr std::size_t kNoteMax = 200;
inline constexpr int kTargetCountMax = 10000;
/// A number goal without a check-in for longer than this is "not updated".
inline constexpr int kStaleDays = 14;

inline bool is_kind(std::string_view v) {
    return std::find(kKinds.begin(), kKinds.end(), v) != kKinds.end();
}

inline bool is_status(std::string_view v) {
    return std::find(kStatuses.begin(), kStatuses.end(), v) != kStatuses.end();
}

}  // namespace Goals::Fields
