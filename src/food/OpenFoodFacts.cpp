/**
 * @file OpenFoodFacts.cpp
 * @brief Bodies for src/food/OpenFoodFacts.hpp.
 */

#include "food/OpenFoodFacts.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include <nlohmann/json.hpp>

namespace Food::Off {

namespace {

constexpr std::string_view kSearchUrl = "https://search.openfoodfacts.org/search";
constexpr std::string_view kProductUrl = "https://world.openfoodfacts.org/api/v2/product/";
constexpr std::string_view kFields =
    "code,product_name,brands,nutriments,serving_size,serving_quantity,nutrition_data_per";
// Open Food Facts asks API users to identify themselves.
constexpr std::string_view kUserAgent = "LifeOS/1.10 (https://life-os.tarassov.me)";

std::string url_encode(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char c : value) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) != 0 || byte == '-' || byte == '_' || byte == '.' || byte == '~') {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(kHex[byte >> 4]);
            out.push_back(kHex[byte & 0x0f]);
        }
    }
    return out;
}

std::string trimmed(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

/// A number from a field that Open Food Facts may hold as a number or a string.
std::optional<double> number(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) {
        return std::nullopt;
    }
    const auto& v = object.at(key);
    if (v.is_number()) {
        return v.get<double>();
    }
    if (v.is_string()) {
        try {
            std::size_t used = 0;
            const double d = std::stod(v.get<std::string>(), &used);
            return used == v.get<std::string>().size() ? std::optional<double>(d) : std::nullopt;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::string text(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) {
        return {};
    }
    const auto& v = object.at(key);
    if (v.is_string()) {
        return trimmed(v.get<std::string>());
    }
    // brands comes as an array from the search service.
    if (v.is_array()) {
        std::string out;
        for (const auto& item : v) {
            if (item.is_string() && !item.get<std::string>().empty()) {
                out += (out.empty() ? "" : ", ") + trimmed(item.get<std::string>());
            }
        }
        return out;
    }
    if (v.is_number()) {
        return std::to_string(v.get<long long>());
    }
    return {};
}

/// nullopt when the product cannot be counted (no kcal).
std::optional<Product> map_product(const nlohmann::json& p) {
    if (!p.is_object() || !p.contains("nutriments") || !p["nutriments"].is_object()) {
        return std::nullopt;
    }
    const auto& n = p["nutriments"];
    const auto kcal = number(n, "energy-kcal_100g");
    if (!kcal.has_value() || *kcal < 0) {
        return std::nullopt;
    }
    Product out;
    out.code = text(p, "code");
    out.name = text(p, "product_name");
    if (out.name.empty()) {
        return std::nullopt;
    }
    out.brand = text(p, "brands");
    out.per = text(p, "nutrition_data_per") == "100ml" ? "100ml" : "100g";
    out.kcal = *kcal;
    const auto at_least_zero = [](std::optional<double> v) -> std::optional<double> {
        return v.has_value() && *v >= 0 ? v : std::nullopt;
    };
    out.protein_g = at_least_zero(number(n, "proteins_100g")).value_or(0);
    out.fat_g = at_least_zero(number(n, "fat_100g")).value_or(0);
    out.carbs_g = at_least_zero(number(n, "carbohydrates_100g")).value_or(0);
    out.fiber_g = at_least_zero(number(n, "fiber_100g"));
    out.sugar_g = at_least_zero(number(n, "sugars_100g"));
    out.salt_g = at_least_zero(number(n, "salt_100g"));
    out.serving_grams = at_least_zero(number(p, "serving_quantity"));
    if (out.serving_grams.has_value() && *out.serving_grams == 0) {
        out.serving_grams.reset();
    }
    out.serving_label = text(p, "serving_size");
    return out;
}

nlohmann::json fetch_json(Http::Transport& transport, const std::string& url) {
    Http::Response response;
    try {
        response = transport.get(url, {{"User-Agent", std::string(kUserAgent)}, {"Accept", "application/json"}});
    } catch (const Http::TransportError& e) {
        throw Unavailable(e.what());
    }
    if (response.status == 404) {
        return nlohmann::json();  // the product route answers 404 for an unknown code
    }
    if (response.status != 200) {
        throw Unavailable("Open Food Facts answered HTTP " + std::to_string(response.status));
    }
    nlohmann::json body = nlohmann::json::parse(response.body, nullptr, /*allow_exceptions=*/false);
    if (body.is_discarded() || !body.is_object()) {
        throw Unavailable("Open Food Facts answered something that is not JSON");
    }
    return body;
}

}  // namespace

std::vector<Product> search(Http::Transport& transport, std::string_view query) {
    std::string q = trimmed(query);
    if (q.size() > kMaxQueryLength) {
        q.resize(kMaxQueryLength);
    }
    std::vector<Product> out;
    if (q.empty()) {
        return out;
    }
    const std::string url = std::string(kSearchUrl) + "?q=" + url_encode(q) +
                            "&page_size=" + std::to_string(kMaxResults) + "&fields=" + std::string(kFields);
    const nlohmann::json body = fetch_json(transport, url);
    if (!body.contains("hits") || !body["hits"].is_array()) {
        throw Unavailable("Open Food Facts search answered without hits");
    }
    for (const auto& hit : body["hits"]) {
        if (auto p = map_product(hit)) {
            out.push_back(std::move(*p));
            if (out.size() >= static_cast<std::size_t>(kMaxResults)) {
                break;
            }
        }
    }
    return out;
}

std::optional<Product> product(Http::Transport& transport, std::string_view code) {
    if (code.empty() || code.size() > 20 ||
        !std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        throw std::invalid_argument("barcode must be 1 to 20 digits");
    }
    const std::string url = std::string(kProductUrl) + std::string(code) + "?fields=" + std::string(kFields);
    const nlohmann::json body = fetch_json(transport, url);
    if (body.is_null() || !body.contains("status") || !body["status"].is_number() || body["status"].get<int>() != 1 ||
        !body.contains("product")) {
        return std::nullopt;
    }
    auto mapped = map_product(body["product"]);
    if (mapped.has_value() && mapped->code.empty()) {
        mapped->code = std::string(code);
    }
    return mapped;
}

}  // namespace Food::Off
