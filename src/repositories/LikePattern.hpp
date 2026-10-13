/**
 * @file LikePattern.hpp
 * @brief User text inside a LIKE / ILIKE pattern: the search boxes match the
 *        text literally, so `%`, `_` and the escape character are quoted.
 */

#pragma once

#include <string>

namespace Repositories {

/// `%`, `_` and `\` quoted by a backslash, for a pattern written with ESCAPE '\\'.
inline std::string escape_like(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '%' || c == '_' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

}  // namespace Repositories
