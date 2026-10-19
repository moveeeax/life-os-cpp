#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "tasks/Repeat.hpp"

using nlohmann::json;
using Tasks::Repeat::Rule;

namespace {

Rule rule(const json& v) {
    Rule r;
    const auto why = Tasks::Repeat::check(v, r);
    EXPECT_FALSE(why.has_value()) << *why;
    return r;
}

std::string next(const json& v, const char* due, const char* done) {
    return Tasks::Repeat::next_due(rule(v), due, done);
}

}  // namespace

TEST(TasksRepeat, ChecksTheShapeOfEachKind) {
    Rule r;
    EXPECT_TRUE(Tasks::Repeat::check(json("daily"), r).has_value());
    EXPECT_TRUE(Tasks::Repeat::check(json{{"kind", "yearly"}}, r).has_value());
    EXPECT_TRUE(Tasks::Repeat::check(json{{"kind", "weekly"}}, r).has_value());
    EXPECT_TRUE(Tasks::Repeat::check(json{{"kind", "weekly"}, {"weekdays", json::array()}}, r).has_value());
    EXPECT_TRUE(Tasks::Repeat::check(json{{"kind", "weekly"}, {"weekdays", json::array({0})}}, r).has_value());
    EXPECT_TRUE(Tasks::Repeat::check(json{{"kind", "weekly"}, {"weekdays", json::array({"1"})}}, r).has_value());
    EXPECT_TRUE(Tasks::Repeat::check(json{{"kind", "monthly"}, {"day", 32}}, r).has_value());
    EXPECT_TRUE(Tasks::Repeat::check(json{{"kind", "every_n_days"}, {"every", 0}}, r).has_value());
    EXPECT_TRUE(Tasks::Repeat::check(json{{"kind", "every_n_days"}, {"every", 366}}, r).has_value());
    EXPECT_FALSE(Tasks::Repeat::check(json{{"kind", "daily"}}, r).has_value());
}

TEST(TasksRepeat, TheStoredFormKeepsOnlyTheFieldsOfTheKind) {
    EXPECT_EQ(Tasks::Repeat::to_json(rule(json{{"kind", "weekly"}, {"weekdays", {5, 1, 5}}, {"day", 3}})),
              (json{{"kind", "weekly"}, {"weekdays", {1, 5}}}));
    EXPECT_EQ(Tasks::Repeat::to_json(rule(json{{"kind", "daily"}, {"every", 4}})), (json{{"kind", "daily"}}));
    EXPECT_EQ(Tasks::Repeat::to_json(rule(json{{"kind", "monthly"}, {"day", 31}})),
              (json{{"kind", "monthly"}, {"day", 31}}));
}

TEST(TasksRepeat, DailyAndEveryNDaysCountFromTheLaterOfDueAndClose) {
    const json daily{{"kind", "daily"}};
    EXPECT_EQ(next(daily, "2026-10-19", "2026-10-19"), "2026-10-20");
    EXPECT_EQ(next(daily, "2026-10-15", "2026-10-19"), "2026-10-20") << "closed late: never in the past";
    EXPECT_EQ(next(daily, "2026-10-22", "2026-10-19"), "2026-10-23") << "closed early: after its own due";
    EXPECT_EQ(next(daily, "", "2026-12-31"), "2027-01-01") << "no due: counts from the close";
    const json every3{{"kind", "every_n_days"}, {"every", 3}};
    EXPECT_EQ(next(every3, "2026-10-19", "2026-10-19"), "2026-10-22");
    EXPECT_EQ(next(every3, "2026-10-10", "2026-10-19"), "2026-10-22");
}

TEST(TasksRepeat, WeeklyTakesTheNextNamedWeekday) {
    // 2026-10-19 is a Monday.
    const json mon_thu{{"kind", "weekly"}, {"weekdays", {1, 4}}};
    EXPECT_EQ(next(mon_thu, "2026-10-19", "2026-10-19"), "2026-10-22");
    EXPECT_EQ(next(mon_thu, "2026-10-22", "2026-10-22"), "2026-10-26");
    EXPECT_EQ(next(json{{"kind", "weekly"}, {"weekdays", json::array({1})}}, "2026-10-19", "2026-10-19"), "2026-10-26")
        << "the same weekday a week on";
    EXPECT_EQ(next(json{{"kind", "weekly"}, {"weekdays", json::array({7})}}, "", "2026-10-19"), "2026-10-25");
}

TEST(TasksRepeat, MonthlyUsesTheLastDayOfAShortMonth) {
    const json on31{{"kind", "monthly"}, {"day", 31}};
    EXPECT_EQ(next(on31, "2026-10-31", "2026-10-31"), "2026-11-30");
    EXPECT_EQ(next(on31, "2027-01-31", "2027-01-31"), "2027-02-28");
    EXPECT_EQ(next(on31, "2028-01-31", "2028-01-31"), "2028-02-29") << "a leap year";
    const json on15{{"kind", "monthly"}, {"day", 15}};
    EXPECT_EQ(next(on15, "2026-10-15", "2026-10-14"), "2026-11-15") << "closed early";
    EXPECT_EQ(next(on15, "2026-10-15", "2026-10-20"), "2026-11-15") << "closed late";
    EXPECT_EQ(next(on15, "", "2026-10-03"), "2026-10-15") << "no due: the next 15th";
    EXPECT_EQ(next(on15, "2026-12-15", "2026-12-15"), "2027-01-15") << "over the year";
}
