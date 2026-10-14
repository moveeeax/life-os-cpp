/**
 * @file Slug.hpp
 * @brief The URL key of a note made from its title, as a tarassov.me post slug:
 *        lower-case ASCII letters and digits joined by single dashes; Russian and
 *        Kazakh Cyrillic transliterated; at most 120 characters, cut at a word.
 */

#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <string_view>

namespace Notes {

inline constexpr std::size_t kSlugMax = 120;

namespace detail {

/// The Latin spelling of a lower-case Cyrillic letter, nullptr for anything else.
inline const char* latin(std::uint32_t cp) {
    switch (cp) {
        case 0x430:
            return "a";
        case 0x431:
            return "b";
        case 0x432:
            return "v";
        case 0x433:
            return "g";
        case 0x434:
            return "d";
        case 0x435:
            return "e";
        case 0x451:
            return "yo";
        case 0x436:
            return "zh";
        case 0x437:
            return "z";
        case 0x438:
            return "i";
        case 0x439:
            return "y";
        case 0x43A:
            return "k";
        case 0x43B:
            return "l";
        case 0x43C:
            return "m";
        case 0x43D:
            return "n";
        case 0x43E:
            return "o";
        case 0x43F:
            return "p";
        case 0x440:
            return "r";
        case 0x441:
            return "s";
        case 0x442:
            return "t";
        case 0x443:
            return "u";
        case 0x444:
            return "f";
        case 0x445:
            return "kh";
        case 0x446:
            return "ts";
        case 0x447:
            return "ch";
        case 0x448:
            return "sh";
        case 0x449:
            return "shch";
        case 0x44A:
            return "";
        case 0x44B:
            return "y";
        case 0x44C:
            return "";
        case 0x44D:
            return "e";
        case 0x44E:
            return "yu";
        case 0x44F:
            return "ya";
        // Kazakh
        case 0x4D9:
            return "a";
        case 0x493:
            return "g";
        case 0x49B:
            return "q";
        case 0x4A3:
            return "ng";
        case 0x4E9:
            return "o";
        case 0x4B1:
        case 0x4AF:
            return "u";
        case 0x4BB:
            return "h";
        case 0x456:
            return "i";
        default:
            return nullptr;
    }
}

/// The lower-case form of an upper-case Cyrillic letter (А–Я, Ё and the Kazakh ones).
inline std::uint32_t lower(std::uint32_t cp) {
    if (cp >= 0x410 && cp <= 0x42F)
        return cp + 0x20;
    if (cp == 0x401)
        return 0x451;
    if (cp == 0x406)
        return 0x456;
    if (cp == 0x4D8 || cp == 0x492 || cp == 0x49A || cp == 0x4A2 || cp == 0x4E8 || cp == 0x4B0 || cp == 0x4AE ||
        cp == 0x4BA)
        return cp + 1;
    return cp;
}

}  // namespace detail

/// The slug of @p title; "note" when nothing in it can be spelled in ASCII.
inline std::string slug_of(std::string_view title) {
    std::string out;
    const auto dash = [&] {
        if (!out.empty() && out.back() != '-')
            out.push_back('-');
    };
    for (std::size_t i = 0; i < title.size();) {
        const auto c = static_cast<unsigned char>(title[i]);
        // The length of the UTF-8 sequence from its lead byte; only two-byte
        // letters (Cyrillic) are decoded, longer ones act as separators.
        const std::size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        std::uint32_t cp = 0;
        if (len == 2 && i + 1 < title.size()) {
            cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(title[i + 1]) & 0x3Fu);
        }
        i += len;
        if (len == 1) {
            if (c >= 'A' && c <= 'Z') {
                out.push_back(static_cast<char>(c - 'A' + 'a'));
            } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                out.push_back(static_cast<char>(c));
            } else {
                dash();
            }
            continue;
        }
        if (const char* l = cp ? detail::latin(detail::lower(cp)) : nullptr) {
            out += l;
        } else {
            dash();
        }
    }
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    if (out.size() > kSlugMax) {
        out.resize(kSlugMax);
        const auto cut = out.rfind('-');
        if (cut != std::string::npos && cut > 0)
            out.resize(cut);
        while (!out.empty() && out.back() == '-')
            out.pop_back();
    }
    return out.empty() ? "note" : out;
}

/// @p base, or base-2, base-3 … when it is in @p taken; kept within 120 characters.
inline std::string free_slug(const std::string& base, const std::set<std::string>& taken) {
    if (!taken.contains(base))
        return base;
    for (int i = 2; i < 100000; ++i) {
        const std::string suffix = "-" + std::to_string(i);
        std::string head = base.substr(0, kSlugMax - suffix.size());
        while (!head.empty() && head.back() == '-')
            head.pop_back();
        const std::string candidate = head + suffix;
        if (!taken.contains(candidate))
            return candidate;
    }
    return base;
}

}  // namespace Notes
