#include <gtest/gtest.h>

#include "tasks/Agenda.hpp"
#include "tasks/Fields.hpp"

using Tasks::Agenda::Item;

namespace {

Item open_item(const char* id, const char* due, const char* next = "step", int age = 1, int idle = 1) {
    return Item{id, "open", due, "", next, age, idle, nlohmann::json{{"id", id}}};
}

std::vector<std::string> ids(const std::vector<nlohmann::json>& rows) {
    std::vector<std::string> out;
    for (const auto& r : rows) {
        out.push_back(r["id"].get<std::string>());
    }
    return out;
}

}  // namespace

TEST(TasksAgenda, GroupsByTheLocalDayWithATwoDayWindow) {
    const std::vector<Item> items{
        open_item("late", "2026-10-06"),
        open_item("today", "2026-10-07"),
        open_item("edge", "2026-10-09"),
        open_item("after", "2026-10-10"),
        open_item("someday", ""),
        Item{"closed", "done", "2026-10-07", "2026-10-07", "", 3, 0, nlohmann::json{{"id", "closed"}}},
        Item{"closed_before", "done", "", "2026-10-06", "", 3, 1, nlohmann::json{{"id", "closed_before"}}},
    };
    const auto g = Tasks::Agenda::group(items, "2026-10-07");
    EXPECT_EQ(ids(g.late), (std::vector<std::string>{"late"}));
    EXPECT_EQ(ids(g.soon), (std::vector<std::string>{"today", "edge"})) << "two days after the date is still soon";
    EXPECT_EQ(ids(g.dated), (std::vector<std::string>{"after"}));
    EXPECT_EQ(ids(g.someday), (std::vector<std::string>{"someday"}));
    EXPECT_EQ(ids(g.done_today), (std::vector<std::string>{"closed"}));
}

TEST(TasksAgenda, ReviewListsOnlyOpenTasksPastTheirThresholds) {
    const std::vector<Item> items{
        open_item("fresh_no_step", "", "", 7, 7),
        open_item("old_no_step", "", "", 8, 8),
        open_item("idle", "", "step", 30, 14),
        open_item("busy", "", "step", 30, 13),
        Item{"done_no_step", "done", "", "2026-10-01", "", 30, 20, nlohmann::json{{"id", "done_no_step"}}},
    };
    const auto g = Tasks::Agenda::group(items, "2026-10-07");
    EXPECT_EQ(ids(g.no_next_step), (std::vector<std::string>{"old_no_step"})) << "older than seven days";
    EXPECT_EQ(ids(g.stale), (std::vector<std::string>{"idle"})) << "untouched for fourteen days or more";
}

TEST(TasksFields, KnowsTheSixAreasAndThreeEfforts) {
    EXPECT_TRUE(Tasks::Fields::is_area("relationships"));
    EXPECT_FALSE(Tasks::Fields::is_area("work"));
    EXPECT_TRUE(Tasks::Fields::is_effort("deep"));
    EXPECT_FALSE(Tasks::Fields::is_effort("1h"));
}
