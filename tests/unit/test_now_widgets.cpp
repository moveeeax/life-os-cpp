/**
 * @file test_now_widgets.cpp
 * @brief The widget board of Now (now/Settings.hpp widgets_problem): at most
 *        six, known kinds, a series with a catalog id, each widget once.
 */

#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "now/Settings.hpp"

using nlohmann::json;
using Now::Settings::widgets_problem;

TEST(NowWidgets, WidgetsValidate) {
    json clean;
    EXPECT_FALSE(widgets_problem(json::array({{{"kind", "series"}, {"source", "fitness:sleep_score"}},
                                              {{"kind", "series"}, {"source", "leetcode:solved"}},
                                              {{"kind", "calendar_week"}, {"source", "ignored"}}}),
                                 clean));
    EXPECT_EQ(clean[2], json({{"kind", "calendar_week"}})) << "only a series keeps a source";
    EXPECT_TRUE(widgets_problem(json::object(), clean));
    EXPECT_TRUE(widgets_problem(json::array({{{"kind", "weather"}}}), clean)) << "unknown kind";
    EXPECT_TRUE(widgets_problem(json::array({{{"kind", "series"}}}), clean)) << "a series needs a source";
    EXPECT_TRUE(widgets_problem(json::array({{{"kind", "series"}, {"source", 42}}}), clean))
        << "a source of the wrong type is a problem, not a throw";
    EXPECT_TRUE(widgets_problem(json::array({{{"kind", "habits"}}, {{"kind", "habits"}}}), clean)) << "once";
    json seven = json::array();
    for (const char* k : {"habits", "spend", "telegram", "trip", "calendar_week"}) {
        seven.push_back({{"kind", k}});
    }
    seven.push_back({{"kind", "series"}, {"source", "fitness:steps"}});
    EXPECT_FALSE(widgets_problem(seven, clean));
    seven.push_back({{"kind", "series"}, {"source", "fitness:sleep"}});
    EXPECT_TRUE(widgets_problem(seven, clean)) << "six at most";
}
