/**
 * @file test_calendar_google.cpp
 * @brief Google Calendar's pure parts (calendar/Google.hpp): the consent URL,
 *        RFC 3339 times, events of a list, free slots around busy events, and
 *        the seal that keeps refresh tokens (utils/Seal.hpp).
 */

#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "calendar/Google.hpp"
#include "utils/Seal.hpp"

namespace {

using nlohmann::json;

}  // namespace

TEST(CalendarGoogle, AuthUrlHasOfflineConsentState) {
    Calendar::Settings s{
        "cid.apps.googleusercontent.com", "sec", "https://life.test/api/v1/calendar/oauth/callback", "k"};
    const std::string url = Calendar::auth_url(s, "abc123");
    EXPECT_EQ(url.rfind("https://accounts.google.com/o/oauth2/v2/auth?", 0), 0u);
    EXPECT_NE(url.find("access_type=offline"), std::string::npos);
    EXPECT_NE(url.find("prompt=consent"), std::string::npos);
    EXPECT_NE(url.find("state=abc123"), std::string::npos);
    EXPECT_NE(url.find("redirect_uri=https%3A%2F%2Flife.test%2Fapi%2Fv1%2Fcalendar%2Foauth%2Fcallback"),
              std::string::npos);
    EXPECT_NE(url.find("scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fcalendar.events%20openid%20email"),
              std::string::npos);
    EXPECT_EQ(url.find("sec"), std::string::npos) << "the secret never goes to the browser";
}

TEST(CalendarGoogle, ParsesRfc3339) {
    EXPECT_EQ(Calendar::parse_time("1970-01-01T00:00:00Z"), 0);
    EXPECT_EQ(Calendar::parse_time("2026-10-29T11:00:00+05:00"), Calendar::parse_time("2026-10-29T06:00:00Z"));
    EXPECT_EQ(Calendar::parse_time("2026-10-29T06:00:00.123Z"), Calendar::parse_time("2026-10-29T06:00:00Z"));
    EXPECT_EQ(*Calendar::parse_time("2026-10-29T06:00:00Z") - *Calendar::parse_time("2026-10-28T06:00:00Z"), 86400);
    EXPECT_FALSE(Calendar::parse_time("2026-10-29"));
    EXPECT_FALSE(Calendar::parse_time("2026-10-29T06:00:00"));
    EXPECT_FALSE(Calendar::parse_time("2026-13-29T06:00:00Z"));
}

TEST(CalendarGoogle, EventViewAllDay) {
    const json list = json::parse(R"({"items":[
      {"id":"b","summary":"Call","start":{"dateTime":"2026-10-29T11:00:00+05:00"},"end":{"dateTime":"2026-10-29T12:00:00+05:00"}},
      {"id":"a","summary":"Birthday","eventType":"birthday","start":{"date":"2026-10-29"},"end":{"date":"2026-10-30"}},
      {"id":"v","summary":"Vacation","start":{"date":"2026-10-30"},"end":{"date":"2026-10-31"}},
      {"id":"s","summary":"Stay","transparency":"transparent","start":{"date":"2026-10-30"},"end":{"date":"2026-10-31"}},
      {"id":"c","status":"cancelled","start":{"dateTime":"2026-10-29T09:00:00Z"},"end":{"dateTime":"2026-10-29T10:00:00Z"}},
      {"id":"d","summary":"Hold","transparency":"transparent","start":{"dateTime":"2026-10-29T13:00:00Z"},"end":{"dateTime":"2026-10-29T14:00:00Z"}}
    ]})");
    const auto events =
        Calendar::parse_events(list, [](const std::string& d) { return d == "2026-10-29" ? 100LL : 200LL; });
    ASSERT_EQ(events.size(), 5u) << "a cancelled one is dropped";
    EXPECT_EQ(events[0].id, "a") << "sorted by start";
    EXPECT_TRUE(events[0].all_day);
    EXPECT_TRUE(events[0].free) << "a birthday does not block the day";
    EXPECT_EQ(events[0].start, 100);
    EXPECT_EQ(Calendar::event_view(events[0])["start"], "2026-10-29");
    for (const auto& e : events) {
        if (e.id == "b") {
            EXPECT_EQ(Calendar::event_view(e)["start"], "2026-10-29T06:00:00Z");
        }
        if (e.id == "d") {
            EXPECT_TRUE(e.free) << "transparent is free";
        }
        if (e.id == "v") {
            EXPECT_FALSE(e.free) << "an all-day event marked busy blocks the day";
        }
        if (e.id == "s") {
            EXPECT_TRUE(e.free) << "an all-day event marked free does not";
        }
    }
}

TEST(CalendarGoogle, FreeSlotsInZone) {
    // 08:00-22:00 of a day as 0..50400; busy 1h at 3h, an overlapping 1h at 3.5h, an event across the end.
    std::vector<Calendar::Event> ev(4);
    ev[0].start = 3 * 3600, ev[0].end = 4 * 3600;
    ev[1].start = 3 * 3600 + 1800, ev[1].end = 5 * 3600;
    ev[2].start = 13 * 3600, ev[2].end = 16 * 3600;
    ev[3].start = 6 * 3600, ev[3].end = 6 * 3600 + 600, ev[3].free = true;
    const auto slots = Calendar::free_slots(ev, 0, 14 * 3600, 30);
    ASSERT_EQ(slots.size(), 2u);
    EXPECT_EQ(slots[0], std::make_pair(0LL, 3 * 3600LL));
    EXPECT_EQ(slots[1], std::make_pair(5 * 3600LL, 13 * 3600LL))
        << "a free event does not split; the last one clips the end";
    EXPECT_TRUE(Calendar::free_slots(ev, 3 * 3600, 3 * 3600 + 1200, 30).empty());
}

TEST(CalendarSeal, RoundTripAndTamper) {
    const std::string key = Utils::Seal::new_key();
    const auto sealed = Utils::Seal::seal("1//refresh-token", key);
    EXPECT_EQ(sealed.ciphertext.find("refresh"), std::string::npos);
    EXPECT_EQ(Utils::Seal::unseal(sealed, key), "1//refresh-token");
    EXPECT_THROW(Utils::Seal::unseal(sealed, Utils::Seal::new_key()), Utils::Seal::SealError);
    auto broken = sealed;
    broken.ciphertext[3] = broken.ciphertext[3] == 'A' ? 'B' : 'A';
    EXPECT_THROW(Utils::Seal::unseal(broken, key), Utils::Seal::SealError);
    EXPECT_THROW(Utils::Seal::seal("x", "short"), Utils::Seal::SealError);
}
