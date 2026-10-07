/**
 * @file Currencies.hpp
 * @brief The smallest unit of a currency: how many decimals an amount in it
 *        may carry and what that unit is called (the kopeck of the ruble, the
 *        satoshi of bitcoin). Fiat exponents follow the ISO 4217 minor-unit
 *        column; a currency of exponent 0 has no smaller unit, so its unit is
 *        the currency itself. Pure: no database, no I/O.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace Money::Currencies {

struct MinorUnit {
    std::string_view code;
    int decimals;
    std::string_view unit;
};

inline constexpr MinorUnit kKnown[] = {
    {"AED", 2, "fils"},    {"AMD", 2, "luma"},   {"AUD", 2, "cent"},    {"AZN", 2, "qepik"},  {"BHD", 3, "fils"},
    {"BTC", 8, "satoshi"}, {"BYN", 2, "kopeck"}, {"CAD", 2, "cent"},    {"CHF", 2, "rappen"}, {"CNY", 2, "fen"},
    {"CZK", 2, "haler"},   {"DKK", 2, "øre"},    {"EUR", 2, "cent"},    {"GBP", 2, "penny"},  {"GEL", 2, "tetri"},
    {"HKD", 2, "cent"},    {"HUF", 2, "fillér"}, {"IDR", 2, "sen"},     {"ILS", 2, "agora"},  {"INR", 2, "paisa"},
    {"JOD", 3, "fils"},    {"JPY", 0, "yen"},    {"KGS", 2, "tyiyn"},   {"KRW", 0, "won"},    {"KWD", 3, "fils"},
    {"KZT", 2, "tiyn"},    {"MYR", 2, "sen"},    {"NOK", 2, "øre"},     {"NZD", 2, "cent"},   {"OMR", 3, "baisa"},
    {"PHP", 2, "centavo"}, {"PLN", 2, "grosz"},  {"RSD", 2, "para"},    {"RUB", 2, "kopeck"}, {"SEK", 2, "öre"},
    {"SGD", 2, "cent"},    {"THB", 2, "satang"}, {"TND", 3, "millime"}, {"TRY", 2, "kuruş"},  {"UAH", 2, "kopiyka"},
    {"USD", 2, "cent"},    {"UZS", 2, "tiyin"},  {"VND", 0, "dong"},
};

/// The minor unit of a code this table knows, or nullopt.
inline std::optional<MinorUnit> known(std::string_view code) {
    for (const auto& m : kKnown) {
        if (m.code == code) {
            return m;
        }
    }
    return std::nullopt;
}

/// Whether @p amount is a whole number of minor units at @p decimals
/// (a double from JSON: the tolerance is a few ulps of the scaled value, far
/// below one unit for any amount the API accepts).
inline bool fits(double amount, int decimals) {
    const double scaled = amount * std::pow(10.0, decimals);
    const double tolerance = std::max(1e-6, std::abs(scaled) * 2e-15);
    return std::abs(scaled - std::round(scaled)) <= tolerance;
}

}  // namespace Money::Currencies
