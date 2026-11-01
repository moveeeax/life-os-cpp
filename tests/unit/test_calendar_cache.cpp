/**
 * @file test_calendar_cache.cpp
 * @brief The day's calendar events behind GET /api/v1/now/day: kept per
 *        user and range, forgotten at once by a change to the calendar, and
 *        the down key after Google failed (InMemoryCache, no Redis).
 */

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "InMemoryCache.hpp"
#include "api/CalendarCache.hpp"
#include "cache/Cache.hpp"
#include "calendar/Google.hpp"

namespace {

namespace CC = Api::CalendarCache;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaa0cc1";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbb0cc1";

std::vector<Calendar::Event> one_event() {
    Calendar::Event e;
    e.id = "e1";
    e.title = "Звонок";
    e.start = 1793253600;
    e.end = 1793257200;
    e.location = "Zoom";
    e.start_text = "2026-10-29T11:00:00+05:00";
    e.end_text = "2026-10-29T12:00:00+05:00";
    Calendar::Event day;
    day.id = "e2";
    day.title = "Birthday";
    day.all_day = true;
    day.free = true;
    day.start_text = "2026-10-29";
    day.end_text = "2026-10-30";
    return {e, day};
}

struct CalendarCacheTest : ::testing::Test {
    void SetUp() override { Cache::install_for_testing(std::make_unique<TestSupport::InMemoryCache>()); }
    void TearDown() override { Cache::reset_for_testing(); }
};

TEST_F(CalendarCacheTest, KeptPerUserAndRange) {
    EXPECT_FALSE(CC::get(kAnna, 100, 200).has_value());
    CC::put(kAnna, 100, 200, one_event());
    const auto kept = CC::get(kAnna, 100, 200);
    ASSERT_TRUE(kept.has_value());
    ASSERT_EQ(kept->size(), 2u);
    EXPECT_EQ((*kept)[0].title, "Звонок");
    EXPECT_EQ((*kept)[0].start, 1793253600);
    EXPECT_EQ((*kept)[0].start_text, "2026-10-29T11:00:00+05:00");
    EXPECT_TRUE((*kept)[1].all_day);
    EXPECT_TRUE((*kept)[1].free);
    EXPECT_FALSE(CC::get(kAnna, 100, 300).has_value()) << "another range";
    EXPECT_FALSE(CC::get(kBoris, 100, 200).has_value()) << "another user";
}

TEST_F(CalendarCacheTest, AChangeForgetsEveryRangeOfTheUser) {
    CC::put(kAnna, 100, 200, one_event());
    CC::put(kAnna, 200, 300, one_event());
    CC::put(kBoris, 100, 200, one_event());
    CC::mark_down(kAnna);
    CC::forget(kAnna);
    EXPECT_FALSE(CC::get(kAnna, 100, 200).has_value());
    EXPECT_FALSE(CC::get(kAnna, 200, 300).has_value());
    EXPECT_FALSE(CC::down(kAnna)) << "a change worked: Google answers again";
    EXPECT_TRUE(CC::get(kBoris, 100, 200).has_value()) << "another user's day stays";
    CC::put(kAnna, 100, 200, {});
    EXPECT_TRUE(CC::get(kAnna, 100, 200).has_value()) << "an empty day is kept too";
}

TEST_F(CalendarCacheTest, DownAfterGoogleFailed) {
    EXPECT_FALSE(CC::down(kAnna));
    CC::mark_down(kAnna);
    EXPECT_TRUE(CC::down(kAnna));
    EXPECT_FALSE(CC::down(kBoris));
}

TEST(CalendarCacheWithoutRedis, NothingIsKept) {
    Cache::reset_for_testing();
    CC::put(kAnna, 100, 200, one_event());
    EXPECT_FALSE(CC::get(kAnna, 100, 200).has_value());
    EXPECT_FALSE(CC::down(kAnna));
    CC::forget(kAnna);
}

TEST(CalendarEventId, GoogleTakesBase32HexOnly) {
    EXPECT_TRUE(Calendar::client_event_id_ok("ani0123456789abcdef0123456789abcdef"));
    EXPECT_FALSE(Calendar::client_event_id_ok("ani1")) << "five characters at least";
    EXPECT_FALSE(Calendar::client_event_id_ok("aniwxyz12345")) << "w to z are not base32hex";
    EXPECT_FALSE(Calendar::client_event_id_ok("ANI0123456")) << "lower case";
    EXPECT_FALSE(Calendar::client_event_id_ok(std::string(1025, 'a')));
}

}  // namespace
