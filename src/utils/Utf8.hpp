/**
 * @file Utf8.hpp
 * @brief Length and cut of UTF-8 text by code point, for limits that must not
 *        split a character: a half character is invalid UTF-8 for JSON dump
 *        and for Postgres, and a byte limit halves what a Cyrillic name may say.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <string>

namespace Utils::Utf8 {

/// Code points of a UTF-8 string (bytes that are not continuation bytes).
inline std::size_t length(const std::string& text) {
    std::size_t n = 0;
    for (const unsigned char c : text) {
        if ((c & 0xC0) != 0x80) {
            ++n;
        }
    }
    return n;
}

/// The first `max_points` code points of a UTF-8 string, never cutting a
/// character in half. A malformed lead byte counts as one byte.
inline std::string cut(const std::string& text, std::size_t max_points) {
    std::size_t i = 0;
    for (std::size_t points = 0; i < text.size() && points < max_points; ++points) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t width = lead < 0x80           ? 1
                                  : (lead >> 5) == 0x6  ? 2
                                  : (lead >> 4) == 0xE  ? 3
                                  : (lead >> 3) == 0x1E ? 4
                                                        : 1;
        i += width;
    }
    return text.substr(0, std::min(i, text.size()));
}

}  // namespace Utils::Utf8
