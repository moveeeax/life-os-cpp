/**
 * @file OpenFoodFacts.hpp
 * @brief Search and product lookup in Open Food Facts, mapped to the shape
 *        of a food item.
 *
 * Search goes to search.openfoodfacts.org (the classic search of
 * world.openfoodfacts.org answered 503 "temporarily unavailable" on
 * 2026-10-06); the product read goes to the v2 API. Products without a kcal
 * value are dropped: they cannot be counted. The data is under the ODbL;
 * see THIRD_PARTY_NOTICES.md.
 */

#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "food/Http.hpp"

namespace Food::Off {

struct Product {
    std::string code;
    std::string name;
    std::string brand;
    std::string per = "100g";  // 100g | 100ml
    double kcal = 0;
    double protein_g = 0;
    double fat_g = 0;
    double carbs_g = 0;
    std::optional<double> fiber_g;
    std::optional<double> sugar_g;
    std::optional<double> salt_g;
    /// One serving as Open Food Facts reports it, when it does.
    std::optional<double> serving_grams;
    std::string serving_label;
};

/// Open Food Facts did not answer usefully: a 5xx, a timeout, not JSON.
struct Unavailable : std::runtime_error {
    explicit Unavailable(const std::string& what) : std::runtime_error(what) {}
};

inline constexpr int kMaxResults = 20;
inline constexpr std::size_t kMaxQueryLength = 80;

/// Products matching @p query (trimmed, cut to kMaxQueryLength), at most kMaxResults.
std::vector<Product> search(Http::Transport& transport, std::string_view query);

/// The product with this barcode (1 to 20 digits), or nullopt when there is none.
/// @throws std::invalid_argument for a code that is not digits.
std::optional<Product> product(Http::Transport& transport, std::string_view code);

}  // namespace Food::Off
