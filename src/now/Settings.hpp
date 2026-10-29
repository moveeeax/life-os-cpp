/**
 * @file Settings.hpp
 * @brief The corner layout of Now (which chip sits in which corner, a corner
 *        possibly empty, every chip at most once) and the quick links of the
 *        Links corner.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "utils/Utf8.hpp"

namespace Now::Settings {

inline constexpr std::array<std::string_view, 4> kCorners = {"tl", "tr", "bl", "br"};
/// The chips a corner may hold; habits is placed through Arrange, never by default.
inline constexpr std::array<std::string_view, 5> kChips = {"body", "goals", "day", "tasks", "habits"};

/// Body top left, Goals top right, Food and money bottom left, Tasks bottom right.
inline nlohmann::json default_corners() {
    return {{"tl", "body"}, {"tr", "goals"}, {"bl", "day"}, {"br", "tasks"}};
}

/// What is wrong with a layout, or nullopt: an object with exactly the four
/// corners, each a known chip or null, no chip twice.
inline std::optional<std::string> corners_problem(const nlohmann::json& corners) {
    if (!corners.is_object() || corners.size() != kCorners.size()) {
        return "corners must be an object with tl, tr, bl and br";
    }
    std::set<std::string> seen;
    for (const auto corner : kCorners) {
        const auto it = corners.find(std::string(corner));
        if (it == corners.end()) {
            return "corners must be an object with tl, tr, bl and br";
        }
        if (it->is_null()) {
            continue;
        }
        if (!it->is_string()) {
            return "a corner holds a chip name or null";
        }
        const std::string chip = it->get<std::string>();
        bool known = false;
        for (const auto k : kChips) {
            known = known || chip == k;
        }
        if (!known) {
            return "a corner holds body, goals, day, tasks, habits or null";
        }
        if (!seen.insert(chip).second) {
            return "a chip sits in one corner at most";
        }
    }
    return std::nullopt;
}

inline constexpr std::size_t kLinksMax = 8;
inline constexpr std::size_t kLinkTitleMax = 40;
inline constexpr std::size_t kLinkUrlMax = 2048;

/// @p s without ASCII whitespace at either end.
inline std::string trimmed(std::string s) {
    const auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!s.empty() && space(s.back())) {
        s.pop_back();
    }
    std::size_t i = 0;
    while (i < s.size() && space(s[i])) {
        ++i;
    }
    return s.substr(i);
}

/// A path of this app ("/money", never "//host") or an http(s) URL. A
/// backslash reads as a slash in a browser and a control character or a
/// space is dropped by its URL parser, so none of them may appear anywhere.
inline bool link_url_ok(const std::string& url) {
    if (url.empty() || url.size() > kLinkUrlMax) {
        return false;
    }
    for (const unsigned char c : url) {
        if (c <= 0x20 || c == 0x7f || c == '\\') {
            return false;
        }
    }
    if (url[0] == '/') {
        return url.size() == 1 || url[1] != '/';
    }
    // The scheme whatever its case: a phone keyboard may capitalise it.
    std::string head = url.substr(0, 8);
    for (auto& c : head) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return head.rfind("http://", 0) == 0 || head.rfind("https://", 0) == 0;
}

/// What is wrong with a links list, or nullopt; @p clean then holds the
/// trimmed copy to store: an array of at most eight {title, url}.
inline std::optional<std::string> links_problem(const nlohmann::json& links, nlohmann::json& clean) {
    if (!links.is_array()) {
        return "links must be an array";
    }
    if (links.size() > kLinksMax) {
        return "at most 8 links";
    }
    clean = nlohmann::json::array();
    for (const auto& l : links) {
        if (!l.is_object() || !l.contains("title") || !l.contains("url") || !l["title"].is_string() ||
            !l["url"].is_string()) {
            return "a link is {title, url}";
        }
        const std::string title = trimmed(l["title"].get<std::string>());
        const std::string url = trimmed(l["url"].get<std::string>());
        if (title.empty() || Utils::Utf8::length(title) > kLinkTitleMax) {
            return "a link title is 1 to 40 characters";
        }
        if (!link_url_ok(url)) {
            return "a link url is a path of this app or an http(s) URL";
        }
        clean.push_back({{"title", title}, {"url", url}});
    }
    return std::nullopt;
}

inline constexpr std::size_t kWidgetsMax = 6;
inline constexpr std::array<std::string_view, 6> kWidgetKinds = {
    "series", "calendar_week", "habits", "spend", "telegram", "trip"};

/// What is wrong with a widget list, or nullopt; @p clean then holds what to
/// store: at most six {kind} or {kind: "series", source: <catalog id>}, no
/// widget twice.
inline std::optional<std::string> widgets_problem(const nlohmann::json& widgets, nlohmann::json& clean) {
    if (!widgets.is_array()) {
        return "widgets must be an array";
    }
    if (widgets.size() > kWidgetsMax) {
        return "at most 6 widgets";
    }
    clean = nlohmann::json::array();
    std::vector<std::string> seen;
    for (const auto& w : widgets) {
        if (!w.is_object() || !w.contains("kind") || !w["kind"].is_string()) {
            return "a widget is {kind} or {kind: series, source}";
        }
        const std::string kind = w["kind"].get<std::string>();
        if (std::find(kWidgetKinds.begin(), kWidgetKinds.end(), kind) == kWidgetKinds.end()) {
            return "unknown widget kind " + kind;
        }
        nlohmann::json one{{"kind", kind}};
        std::string key = kind;
        if (kind == "series") {
            const std::string source = w.value("source", std::string());
            if (source.empty() || source.size() > 120 || source.find(':') == std::string::npos) {
                return "a series widget takes a catalog id as its source";
            }
            one["source"] = source;
            key += ":" + source;
        }
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
            return "a widget is on the board once";
        }
        seen.push_back(key);
        clean.push_back(one);
    }
    return std::nullopt;
}

}  // namespace Now::Settings
