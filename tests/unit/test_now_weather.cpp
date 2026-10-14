/**
 * @file test_now_weather.cpp
 * @brief The pure parts of the Now settings and weather: words for the WMO
 *        weather codes, the place out of a geocoder answer, the weather block
 *        out of a forecast, the cache key, and the corner layout rules.
 */

#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "now/Settings.hpp"
#include "now/Weather.hpp"

using json = nlohmann::json;

TEST(NowWeather, WordsForTheWmoCodes) {
    EXPECT_EQ(Now::Weather::words(0), "clear");
    EXPECT_EQ(Now::Weather::words(2), "partly cloudy");
    EXPECT_EQ(Now::Weather::words(45), "fog");
    EXPECT_EQ(Now::Weather::words(63), "rain");
    EXPECT_EQ(Now::Weather::words(75), "snow");
    EXPECT_EQ(Now::Weather::words(95), "thunderstorm");
    EXPECT_EQ(Now::Weather::words(42), "") << "a code outside the table has no words";
}

TEST(NowWeather, ThePlaceOfAGeocoderAnswer) {
    // The shape geocoding-api.open-meteo.com/v1/search answered for Almaty on 2026-10-14.
    const json answer = json::parse(R"({"results":[{"id":1526384,"name":"Almaty","latitude":43.25249,
        "longitude":76.9115,"country_code":"KZ","timezone":"Asia/Almaty","country":"Kazakhstan"}]})");
    const auto p = Now::Weather::place_of(answer);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->city, "Almaty");
    EXPECT_EQ(p->country_code, "KZ");
    EXPECT_DOUBLE_EQ(p->latitude, 43.25249);
    EXPECT_EQ(p->timezone, "Asia/Almaty");
    EXPECT_FALSE(Now::Weather::place_of(json::parse(R"({"generationtime_ms":0.2})")).has_value()) << "no match";
    EXPECT_FALSE(Now::Weather::place_of(json::parse(R"({"results":[{"name":"X"}]})")).has_value()) << "no coordinates";
}

TEST(NowWeather, TheBlockOfAForecast) {
    // The shape api.open-meteo.com/v1/forecast answered for Almaty on 2026-10-14.
    const json forecast = json::parse(R"({"timezone":"Asia/Almaty",
        "current":{"time":"2026-10-14T06:00","interval":900,"temperature_2m":9.2,"weather_code":45,"is_day":0},
        "daily":{"time":["2026-10-14"],"temperature_2m_max":[19.2],"temperature_2m_min":[9.0]}})");
    const auto b = Now::Weather::block_of(forecast, "Almaty");
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ((*b)["place"], "Almaty");
    EXPECT_DOUBLE_EQ((*b)["temperature"].get<double>(), 9.2);
    EXPECT_DOUBLE_EQ((*b)["max"].get<double>(), 19.2);
    EXPECT_DOUBLE_EQ((*b)["min"].get<double>(), 9.0);
    EXPECT_EQ((*b)["code"], 45);
    EXPECT_EQ((*b)["words"], "fog");
    EXPECT_EQ((*b)["is_day"], false);
    EXPECT_EQ((*b)["time"], "2026-10-14T06:00");
    EXPECT_FALSE(Now::Weather::block_of(json::parse(R"({"current":{}})"), "Almaty").has_value());
    EXPECT_FALSE(Now::Weather::block_of(json::parse(R"({"error":true,"reason":"x"})"), "Almaty").has_value());
}

TEST(NowWeather, OneCacheKeyPerCoordinatesToTwoDecimals) {
    EXPECT_EQ(Now::Weather::cache_key(43.25249, 76.9115), "now:weather:43.25:76.91");
    EXPECT_EQ(Now::Weather::cache_key(-33.8688, 151.2093), "now:weather:-33.87:151.21");
}

TEST(NowWeather, AFailureHasItsOwnKey) {
    EXPECT_EQ(Now::Weather::down_key(43.25249, 76.9115), "now:weather:down:43.25:76.91");
}

TEST(NowWeather, TheCityGoesIntoTheUrlEscaped) {
    EXPECT_EQ(Now::Weather::geocode_url("Sankt Peterburg"),
              "https://geocoding-api.open-meteo.com/v1/search?name=Sankt%20Peterburg&count=1&language=en&format=json");
    EXPECT_NE(Now::Weather::geocode_url("Алматы").find("%D0%90"), std::string::npos) << "UTF-8 bytes escaped";
}

TEST(NowSettings, TheDefaultLayout) {
    EXPECT_EQ(Now::Settings::default_corners(),
              json({{"tl", "body"}, {"tr", "goals"}, {"bl", "day"}, {"br", "tasks"}}));
}

TEST(NowSettings, ACornerLayoutIsChecked) {
    using Now::Settings::corners_problem;
    EXPECT_FALSE(corners_problem(json({{"tl", "tasks"}, {"tr", nullptr}, {"bl", "day"}, {"br", "body"}})))
        << "a hidden corner and moved chips";
    EXPECT_TRUE(corners_problem(json({{"tl", "tasks"}, {"tr", "tasks"}, {"bl", "day"}, {"br", "body"}})))
        << "a chip twice";
    EXPECT_TRUE(corners_problem(json({{"tl", "travel"}, {"tr", nullptr}, {"bl", nullptr}, {"br", nullptr}})))
        << "an unknown chip";
    EXPECT_TRUE(corners_problem(json({{"tl", "body"}, {"tr", "goals"}, {"bl", "day"}}))) << "a corner missing";
    EXPECT_TRUE(
        corners_problem(json({{"tl", "body"}, {"tr", "goals"}, {"bl", "day"}, {"br", "tasks"}, {"mid", nullptr}})))
        << "an unknown corner";
    EXPECT_TRUE(corners_problem(json::array())) << "not an object";
}
