/**
 * @file Merchant.hpp
 * @brief The key under which a merchant is remembered: "Sukiya Phuket  (THB)"
 *        and "sukiya phuket thb" are the same merchant. Lower case, letters
 *        and digits of any script, single spaces; `&` and `'` kept because
 *        "M&M's" differs from "MMS".
 */

#pragma once

#include <cctype>
#include <string>

#include "utils/Utf8.hpp"

namespace Money {

inline std::string merchant_key(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool pending_space = false;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) {
            const char c = static_cast<char>(std::tolower(lead));
            if (std::isalnum(lead) || c == '&' || c == '\'') {
                if (pending_space && !out.empty()) {
                    out.push_back(' ');
                }
                pending_space = false;
                out.push_back(c);
            } else {
                pending_space = true;
            }
            ++i;
            continue;
        }
        // A multi-byte character is kept as it is: Cyrillic, Thai, CJK names
        // stay readable, and the lower-casing of those is not worth a library.
        const std::size_t width = (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
        if (pending_space && !out.empty()) {
            out.push_back(' ');
        }
        pending_space = false;
        out.append(text, i, width);
        i += width;
    }
    return Utils::Utf8::cut(out, 200);
}

}  // namespace Money
