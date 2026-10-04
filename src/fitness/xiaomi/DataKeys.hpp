/**
 * @file DataKeys.hpp
 * @brief Data type keys the Mi Fitness cloud understands.
 *
 * The list mirrors upstream. An unknown key is cut off before the network:
 * the cloud would answer it with emptiness or an error, and a typo would be
 * indistinguishable from missing data.
 */

#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace Xiaomi {

inline constexpr std::array<std::string_view, 9> kDataKeys = {"steps",
                                                              "calories",
                                                              "sleep",
                                                              "weight",
                                                              "heart_rate",
                                                              "spo2",
                                                              "stress",
                                                              "resting_heart_rate",
                                                              "abnormal_heart_beat"};

inline bool is_known_data_key(std::string_view key) {
    return std::find(kDataKeys.begin(), kDataKeys.end(), key) != kDataKeys.end();
}

}  // namespace Xiaomi
