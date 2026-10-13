/**
 * @file Schengen.hpp
 * @brief The Schengen area as ISO 3166-1 alpha-2 codes, for the rule group
 *        key SCHENGEN. 29 members, Bulgaria and Romania full members since
 *        2025-01-01; checked 2026-10-13 against
 *        https://en.wikipedia.org/wiki/Schengen_Area and
 *        https://epthinktank.eu/2025/05/28/state-of-the-schengen-area/timeline-of-schengen-enlargement
 */

#pragma once

#include <set>
#include <string>

namespace Travel {

inline const std::set<std::string>& schengen() {
    static const std::set<std::string> members{
        "AT", "BE", "BG", "CH", "CZ", "DE", "DK", "EE", "ES", "FI", "FR", "GR", "HR", "HU", "IS",
        "IT", "LI", "LT", "LU", "LV", "MT", "NL", "NO", "PL", "PT", "RO", "SE", "SI", "SK",
    };
    return members;
}

/// The countries of a rule: ISO codes as given, the group key SCHENGEN expanded.
inline std::set<std::string> expand_countries(const std::set<std::string>& codes) {
    std::set<std::string> out;
    for (const auto& c : codes) {
        if (c == "SCHENGEN") {
            out.insert(schengen().begin(), schengen().end());
        } else {
            out.insert(c);
        }
    }
    return out;
}

}  // namespace Travel
