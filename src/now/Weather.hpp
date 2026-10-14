/**
 * @file Weather.hpp
 * @brief The weather line of Now from Open-Meteo: the city geocoded once
 *        (geocoding-api.open-meteo.com), today's weather at its coordinates
 *        (api.open-meteo.com/v1/forecast). No key; the free tier is for
 *        non-commercial use, which a personal Life OS is.
 */

#pragma once

#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "net/Http.hpp"

namespace Now::Weather {

/// The upstream did not answer with what we need.
struct Unavailable : std::runtime_error {
    explicit Unavailable(const std::string& what) : std::runtime_error(what) {}
};

/// The place the geocoder found.
struct Place {
    std::string city;
    std::string country_code;
    double latitude = 0;
    double longitude = 0;
    std::string timezone;
};

/// How long one forecast answer is kept, per coordinates.
inline constexpr long kCacheSeconds = 3600;
/// How long a failed call is remembered, so a down service is not asked on every open.
inline constexpr long kDownSeconds = 300;

/// Words for the WMO weather interpretation codes Open-Meteo returns
/// (open-meteo.com/en/docs, "WMO Weather interpretation codes"); "" outside the table.
inline std::string words(int code) {
    switch (code) {
        case 0:
            return "clear";
        case 1:
            return "mainly clear";
        case 2:
            return "partly cloudy";
        case 3:
            return "overcast";
        case 45:
        case 48:
            return "fog";
        case 51:
        case 53:
        case 55:
            return "drizzle";
        case 56:
        case 57:
            return "freezing drizzle";
        case 61:
        case 63:
        case 65:
            return "rain";
        case 66:
        case 67:
            return "freezing rain";
        case 71:
        case 73:
        case 75:
            return "snow";
        case 77:
            return "snow grains";
        case 80:
        case 81:
        case 82:
            return "rain showers";
        case 85:
        case 86:
            return "snow showers";
        case 95:
            return "thunderstorm";
        case 96:
        case 99:
            return "thunderstorm with hail";
        default:
            return "";
    }
}

/// Percent-encodes every byte outside the unreserved set (RFC 3986), UTF-8 included.
inline std::string escape(const std::string& text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : text) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

inline std::string geocode_url(const std::string& city) {
    return "https://geocoding-api.open-meteo.com/v1/search?name=" + escape(city) + "&count=1&language=en&format=json";
}

inline std::string forecast_url(double latitude, double longitude) {
    char buf[256];
    std::snprintf(buf,
                  sizeof buf,
                  "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                  "&current=temperature_2m,weather_code,is_day"
                  "&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1",
                  latitude,
                  longitude);
    return buf;
}

/// The key of a cached forecast: the coordinates to two decimals (about a kilometre).
inline std::string cache_key(double latitude, double longitude) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "now:weather:%.2f:%.2f", latitude, longitude);
    return buf;
}

/// The key that remembers a failed call for these coordinates.
inline std::string down_key(double latitude, double longitude) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "now:weather:down:%.2f:%.2f", latitude, longitude);
    return buf;
}

/// The first @p max_bytes of UTF-8 @p text, never half a character.
inline std::string utf8_prefix(const std::string& text, std::size_t max_bytes) {
    if (text.size() <= max_bytes) {
        return text;
    }
    std::size_t end = max_bytes;
    // Step back over continuation bytes (10xxxxxx) to the start of a character.
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    return text.substr(0, end);
}

/// A string field of @p obj, or "" when it is missing or of another type.
inline std::string text_field(const nlohmann::json& obj, const char* key) {
    const auto it = obj.find(key);
    return it != obj.end() && it->is_string() ? it->get<std::string>() : std::string();
}

/// The first match of a geocoder answer, or nullopt when there is none, it has
/// no name, or its coordinates are missing or out of range. Odd optional fields
/// are dropped: the country code unless it is two capitals, a non-string zone.
inline std::optional<Place> place_of(const nlohmann::json& answer) {
    if (!answer.is_object() || !answer.contains("results") || !answer["results"].is_array() ||
        answer["results"].empty()) {
        return std::nullopt;
    }
    const auto& r = answer["results"][0];
    if (!r.is_object() || !r.contains("name") || !r["name"].is_string() || !r.contains("latitude") ||
        !r["latitude"].is_number() || !r.contains("longitude") || !r["longitude"].is_number()) {
        return std::nullopt;
    }
    Place p;
    p.city = utf8_prefix(r["name"].get<std::string>(), 120);
    p.latitude = r["latitude"].get<double>();
    p.longitude = r["longitude"].get<double>();
    if (p.city.empty() || p.latitude < -90 || p.latitude > 90 || p.longitude < -180 || p.longitude > 180) {
        return std::nullopt;
    }
    p.country_code = text_field(r, "country_code");
    const auto upper = [](char c) { return c >= 'A' && c <= 'Z'; };
    if (p.country_code.size() != 2 || !upper(p.country_code[0]) || !upper(p.country_code[1])) {
        p.country_code.clear();
    }
    p.timezone = utf8_prefix(text_field(r, "timezone"), 64);
    return p;
}

/// {place, temperature, min, max, code, words, is_day, time} of a forecast answer, or nullopt
/// when a field is missing.
inline std::optional<nlohmann::json> block_of(const nlohmann::json& forecast, const std::string& place) {
    if (!forecast.is_object() || !forecast.contains("current") || !forecast["current"].is_object()) {
        return std::nullopt;
    }
    const auto& cur = forecast["current"];
    if (!cur.contains("temperature_2m") || !cur["temperature_2m"].is_number() || !cur.contains("weather_code") ||
        !cur["weather_code"].is_number_integer()) {
        return std::nullopt;
    }
    nlohmann::json out = {
        {"place", place},
        {"temperature", cur["temperature_2m"].get<double>()},
        {"code", cur["weather_code"].get<int>()},
        {"words", words(cur["weather_code"].get<int>())},
        {"is_day", !(cur.contains("is_day") && cur["is_day"].is_number_integer() && cur["is_day"].get<int>() == 0)},
        {"time", text_field(cur, "time")},
        {"min", nullptr},
        {"max", nullptr},
    };
    if (forecast.contains("daily") && forecast["daily"].is_object()) {
        const auto& d = forecast["daily"];
        for (const auto& [from, to] :
             {std::pair{"temperature_2m_min", "min"}, std::pair{"temperature_2m_max", "max"}}) {
            if (d.contains(from) && d[from].is_array() && !d[from].empty() && d[from][0].is_number()) {
                out[to] = d[from][0].get<double>();
            }
        }
    }
    return out;
}

/// GET @p url and parse its JSON. @throws Unavailable on a transport error, a non-200 or bad JSON.
inline nlohmann::json get_json(Net::Http::Transport& transport, const std::string& url) {
    Net::Http::Response r;
    try {
        r = transport.get(url, {{"Accept", "application/json"}});
    } catch (const Net::Http::TransportError& e) {
        throw Unavailable(e.what());
    }
    if (r.status != 200) {
        throw Unavailable("HTTP " + std::to_string(r.status));
    }
    auto j = nlohmann::json::parse(r.body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) {
        throw Unavailable("not JSON");
    }
    return j;
}

/// The place of @p city, nullopt when the geocoder knows none. @throws Unavailable.
inline std::optional<Place> geocode(Net::Http::Transport& transport, const std::string& city) {
    return place_of(get_json(transport, geocode_url(city)));
}

/// Today's weather block at the coordinates. @throws Unavailable (also for an answer missing fields).
inline nlohmann::json forecast(Net::Http::Transport& transport,
                               double latitude,
                               double longitude,
                               const std::string& place) {
    const auto block = block_of(get_json(transport, forecast_url(latitude, longitude)), place);
    if (!block) {
        throw Unavailable("the forecast lacks the current weather");
    }
    return *block;
}

}  // namespace Now::Weather
