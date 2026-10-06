/**
 * @file test_open_food_facts.cpp
 * @brief The Open Food Facts client over a scripted transport, with answers
 *        recorded on 2026-10-06.
 */

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "food/Http.hpp"
#include "food/OpenFoodFacts.hpp"

namespace {

std::string fixture(const char* name) {
    std::ifstream in(std::string("tests/fixtures/") + name);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

class ScriptedTransport : public Food::Http::Transport {
public:
    std::vector<Food::Http::Response> replies;
    std::vector<std::string> urls;
    std::vector<std::vector<std::pair<std::string, std::string>>> headers;
    bool fail = false;

    Food::Http::Response get(const std::string& url,
                             const std::vector<std::pair<std::string, std::string>>& h) override {
        urls.push_back(url);
        headers.push_back(h);
        if (fail) {
            throw Food::Http::TransportError("Could not resolve host");
        }
        if (replies.empty()) {
            throw std::runtime_error("ScriptedTransport: unexpected request to " + url);
        }
        auto r = replies.front();
        replies.erase(replies.begin());
        return r;
    }

    Food::Http::Response post_json(const std::string& url,
                                   const std::string&,
                                   const std::vector<std::pair<std::string, std::string>>& h,
                                   long) override {
        return get(url, h);
    }
};

}  // namespace

TEST(OpenFoodFacts, SearchMapsTheRecordedAnswer) {
    ScriptedTransport t;
    t.replies.push_back({200, fixture("off_search.json")});
    const auto products = Food::Off::search(t, "  tonkatsu curry ");
    ASSERT_EQ(products.size(), 3u);
    EXPECT_EQ(products[0].code, "0074410285159");
    EXPECT_EQ(products[0].name, "Hot Pot Base");
    EXPECT_EQ(products[0].brand, "Tonkatsu");
    EXPECT_EQ(products[0].per, "100g");
    EXPECT_DOUBLE_EQ(products[0].kcal, 36);
    EXPECT_DOUBLE_EQ(products[0].fiber_g.value(), 0.8);
    EXPECT_FALSE(products[1].fiber_g.has_value());
    EXPECT_EQ(products[2].brand, "Bull-Dog Sauce Co.  Ltd., Bertolli");

    ASSERT_EQ(t.urls.size(), 1u);
    EXPECT_EQ(t.urls[0].rfind("https://search.openfoodfacts.org/search?q=tonkatsu%20curry&page_size=20&fields=", 0),
              0u);
    bool agent = false;
    for (const auto& [k, v] : t.headers[0]) {
        agent = agent || (k == "User-Agent" && v.rfind("LifeOS/", 0) == 0);
    }
    EXPECT_TRUE(agent);
}

TEST(OpenFoodFacts, ProductMapsTheRecordedAnswer) {
    ScriptedTransport t;
    t.replies.push_back({200, fixture("off_product.json")});
    const auto p = Food::Off::product(t, "3017624010701");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->name, "Nutella");
    EXPECT_EQ(p->brand, "Ferrero");
    EXPECT_DOUBLE_EQ(p->kcal, 539);
    EXPECT_DOUBLE_EQ(p->salt_g.value(), 0.1075);
    EXPECT_FALSE(p->fiber_g.has_value());
    EXPECT_FALSE(p->serving_grams.has_value());
    EXPECT_EQ(
        t.urls[0],
        "https://world.openfoodfacts.org/api/v2/product/"
        "3017624010701?fields=code,product_name,brands,nutriments,serving_size,serving_quantity,nutrition_data_per");
}

TEST(OpenFoodFacts, ProductWithoutKcalOrNameIsDropped) {
    ScriptedTransport t;
    t.replies.push_back({200, R"({"hits":[
        {"code":"1","product_name":"No kcal","nutriments":{"proteins_100g":5}},
        {"code":"2","nutriments":{"energy-kcal_100g":100}},
        {"code":"3","product_name":"Fine","nutriments":{"energy-kcal_100g":100}}]})"});
    const auto products = Food::Off::search(t, "x");
    ASSERT_EQ(products.size(), 1u);
    EXPECT_EQ(products[0].code, "3");
    EXPECT_DOUBLE_EQ(products[0].protein_g, 0) << "missing macros count as zero";
}

TEST(OpenFoodFacts, NumbersAsStringsAndNegativesAreHandled) {
    ScriptedTransport t;
    t.replies.push_back({200, R"({"hits":[{"code":"1","product_name":"S","nutrition_data_per":"100ml",
        "serving_quantity":"250","serving_size":"1 glass",
        "nutriments":{"energy-kcal_100g":"42","proteins_100g":"abc","fat_100g":-1,"sugars_100g":"3.5"}}]})"});
    const auto products = Food::Off::search(t, "x");
    ASSERT_EQ(products.size(), 1u);
    EXPECT_EQ(products[0].per, "100ml");
    EXPECT_DOUBLE_EQ(products[0].kcal, 42);
    EXPECT_DOUBLE_EQ(products[0].protein_g, 0);
    EXPECT_DOUBLE_EQ(products[0].fat_g, 0);
    EXPECT_DOUBLE_EQ(products[0].sugar_g.value(), 3.5);
    EXPECT_DOUBLE_EQ(products[0].serving_grams.value(), 250);
    EXPECT_EQ(products[0].serving_label, "1 glass");
}

TEST(OpenFoodFacts, ServerErrorsAndNonJsonAreUnavailable) {
    {
        ScriptedTransport t;
        t.replies.push_back({503, "<html>busy</html>"});
        EXPECT_THROW(Food::Off::search(t, "x"), Food::Off::Unavailable);
    }
    {
        ScriptedTransport t;
        t.replies.push_back({200, "<html>maintenance</html>"});
        EXPECT_THROW(Food::Off::search(t, "x"), Food::Off::Unavailable);
    }
    {
        ScriptedTransport t;
        t.fail = true;
        EXPECT_THROW(Food::Off::product(t, "123"), Food::Off::Unavailable);
    }
}

TEST(OpenFoodFacts, UnknownProductIsNullopt) {
    ScriptedTransport t;
    t.replies.push_back({200, R"({"code":"00000000","status":0,"status_verbose":"no code or invalid code"})"});
    EXPECT_FALSE(Food::Off::product(t, "00000000").has_value());
    ScriptedTransport gone;
    gone.replies.push_back({404, ""});
    EXPECT_FALSE(Food::Off::product(gone, "1").has_value());
}

TEST(OpenFoodFacts, QueryAndBarcodeAreValidated) {
    ScriptedTransport t;
    EXPECT_TRUE(Food::Off::search(t, "   ").empty());
    EXPECT_TRUE(t.urls.empty());
    EXPECT_THROW(Food::Off::product(t, "12a"), std::invalid_argument);
    EXPECT_THROW(Food::Off::product(t, ""), std::invalid_argument);
    EXPECT_THROW(Food::Off::product(t, std::string(21, '1')), std::invalid_argument);
    EXPECT_TRUE(t.urls.empty());

    t.replies.push_back({200, R"({"hits":[]})"});
    Food::Off::search(t, std::string(100, 'a'));
    EXPECT_NE(t.urls[0].find("q=" + std::string(80, 'a') + "&"), std::string::npos) << "cut to 80 characters";
}
