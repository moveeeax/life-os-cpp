/**
 * @file test_calendar.cpp
 * @brief Google Calendar against a real database and a scripted Google: a
 *        state is one-time, bound and short-lived; a refresh token sealed at
 *        connect gives access tokens; Google's refusal marks the link broken
 *        once and is not retried; events come back parsed in the user's zone.
 */

#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "calendar/Access.hpp"
#include "database/Database.hpp"
#include "net/Http.hpp"
#include "repositories/calendar/LinkRepository.hpp"
#include "test_helpers.hpp"
#include "utils/Seal.hpp"

namespace {

using nlohmann::json;
using Repositories::Calendar::LinkRepository;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaa0ca1";
const std::string kKey = Utils::Seal::new_key();

struct ScriptedGoogle : Net::Http::Transport {
    bool refuse = false;
    /// The events come in two pages.
    bool paged = false;
    int refreshes = 0;
    std::vector<std::string> urls;
    Net::Http::Response get(const std::string&, const std::vector<std::pair<std::string, std::string>>&) override {
        return {404, ""};
    }
    Net::Http::Response post_json(const std::string&,
                                  const std::string&,
                                  const std::vector<std::pair<std::string, std::string>>&,
                                  long) override {
        return {404, ""};
    }
    Net::Http::Response send(const Net::Http::Request& r) override {
        urls.push_back(r.method + " " + r.url);
        if (r.url == "https://oauth2.googleapis.com/token") {
            ++refreshes;
            if (refuse) {
                return {400, R"({"error":"invalid_grant","error_description":"Token has been expired or revoked."})"};
            }
            return {200, R"({"access_token":"ya29.test","expires_in":3599,"token_type":"Bearer"})"};
        }
        if (paged && r.url.rfind("https://www.googleapis.com/calendar/v3/calendars/primary/events?", 0) == 0) {
            if (r.url.find("pageToken=p2") != std::string::npos) {
                return {200, R"({"items":[
              {"id":"e3","summary":"Late","start":{"dateTime":"2026-10-29T20:00:00+05:00"},"end":{"dateTime":"2026-10-29T21:00:00+05:00"}}]})"};
            }
            return {200, R"({"nextPageToken":"p2","items":[
              {"id":"e1","summary":"Call with Anna","start":{"dateTime":"2026-10-29T11:00:00+05:00"},"end":{"dateTime":"2026-10-29T12:00:00+05:00"}}]})"};
        }
        if (r.url.rfind("https://www.googleapis.com/calendar/v3/calendars/primary/events?", 0) == 0) {
            return {200, R"({"items":[
              {"id":"e1","summary":"Call with Anna","start":{"dateTime":"2026-10-29T11:00:00+05:00"},"end":{"dateTime":"2026-10-29T12:00:00+05:00"}},
              {"id":"e2","summary":"Birthday","start":{"date":"2026-10-29"},"end":{"date":"2026-10-30"}}]})"};
        }
        return {404, ""};
    }
};

class CalendarLink : public TestHelpers::CoreBackedTest {
protected:
    ScriptedGoogle google;
    LinkRepository links;

    std::string config_file_name() const override { return "calendar_test_config.json"; }
    void config_overrides(json& cfg) override {
        cfg["calendar"]["google"]["client_id"] = "cid";
        cfg["calendar"]["google"]["client_secret"] = "secret";
        cfg["calendar"]["google"]["redirect_url"] = "https://life.test/api/v1/calendar/oauth/callback";
        cfg["calendar"]["google"]["token_key"] = kKey;
    }
    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("DELETE FROM calendar_links");
            txn.exec("DELETE FROM calendar_states");
            return true;
        });
        Calendar::forget(kAnna);
        Net::Http::install_for_testing(&google);
    }
    void TearDown() override {
        Net::Http::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }
    void linked() {
        const auto sealed = Utils::Seal::seal("1//refresh", kKey);
        links.set(kAnna, "anna@example.com", sealed.ciphertext, sealed.nonce);
    }
};

}  // namespace

TEST_F(CalendarLink, StateIsOneUseAndBound) {
    const std::string state = links.new_state(kAnna);
    EXPECT_FALSE(links.take_state("someone-elses").has_value());
    EXPECT_EQ(links.take_state(state), kAnna);
    EXPECT_FALSE(links.take_state(state).has_value()) << "one use";
    const std::string old = links.new_state(kAnna);
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("UPDATE calendar_states SET created_at = now() - interval '11 minutes' WHERE state = $1", old);
        return true;
    });
    EXPECT_FALSE(links.take_state(old).has_value()) << "ten minutes at most";
}

TEST_F(CalendarLink, StateNeedsTheSameSignedInUser) {
    // A consent link sent to someone else must not attach their calendar to the sender.
    const std::string state = links.new_state(kAnna);
    EXPECT_FALSE(links.take_state(state, "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbb0ca1").has_value());
    EXPECT_FALSE(links.take_state(state, kAnna).has_value()) << "a refused state is spent";
    const std::string next = links.new_state(kAnna);
    EXPECT_FALSE(links.take_state(next, "").has_value()) << "no session, no link";
    const std::string mine = links.new_state(kAnna);
    EXPECT_EQ(links.take_state(mine, kAnna), kAnna);
}

TEST_F(CalendarLink, LocalTimeOnTheZonesClock) {
    // 2026-10-29T10:00:00Z.
    EXPECT_EQ(links.local_rfc3339(1793268000, "Asia/Tashkent"), "2026-10-29T15:00:00+05:00");
    EXPECT_EQ(links.local_rfc3339(1793268000, "America/New_York"), "2026-10-29T06:00:00-04:00");
    EXPECT_EQ(links.local_rfc3339(1793268000, "Asia/Kolkata"), "2026-10-29T15:30:00+05:30");
}

TEST_F(CalendarLink, ConnectStoresSealedToken) {
    linked();
    const std::string raw = Database::get().execute_read(
        [](auto& txn) { return txn.exec("SELECT token_sealed FROM calendar_links")[0][0].template as<std::string>(); });
    EXPECT_EQ(raw.find("refresh"), std::string::npos) << "never in the clear";
    auto token = Calendar::access(google, kAnna);
    ASSERT_TRUE(std::holds_alternative<std::string>(token));
    EXPECT_EQ(std::get<std::string>(token), "ya29.test");
    Calendar::access(google, kAnna);
    EXPECT_EQ(google.refreshes, 1) << "the access token is kept in memory";
}

TEST_F(CalendarLink, ALinkChangedElsewhereIsNotServedFromTheCache) {
    // Another replica disconnects or connects again; this one's cache must not outlive it.
    linked();
    ASSERT_TRUE(std::holds_alternative<std::string>(Calendar::access(google, kAnna)));
    links.remove(kAnna);
    auto gone = Calendar::access(google, kAnna);
    ASSERT_TRUE(std::holds_alternative<Calendar::Problem>(gone));
    EXPECT_EQ(std::get<Calendar::Problem>(gone).code, "calendar_not_linked");
    linked();
    ASSERT_TRUE(std::holds_alternative<std::string>(Calendar::access(google, kAnna)));
    EXPECT_EQ(google.refreshes, 2) << "a new link gets a new access token";
}

TEST_F(CalendarLink, RefreshRefusedMarksBroken) {
    linked();
    google.refuse = true;
    auto first = Calendar::access(google, kAnna);
    ASSERT_TRUE(std::holds_alternative<Calendar::Problem>(first));
    EXPECT_EQ(std::get<Calendar::Problem>(first).code, "calendar_disconnected");
    EXPECT_TRUE((*links.status(kAnna))["broken"].get<bool>());
    Calendar::access(google, kAnna);
    EXPECT_EQ(google.refreshes, 1) << "a broken link is not retried";
    google.refuse = false;
    linked();
    EXPECT_TRUE(std::holds_alternative<std::string>(Calendar::access(google, kAnna))) << "connecting again mends it";
}

TEST_F(CalendarLink, EventsInTheUsersZone) {
    linked();
    const long long from = links.local_instant("2026-10-29", 0, "Asia/Tashkent");
    const long long to = links.local_instant("2026-10-29", 24, "Asia/Tashkent");
    EXPECT_EQ(to - from, 86400);
    auto out = Calendar::events(google, kAnna, from, to, "Asia/Tashkent");
    ASSERT_TRUE(std::holds_alternative<std::vector<Calendar::Event>>(out));
    const auto& ev = std::get<std::vector<Calendar::Event>>(out);
    ASSERT_EQ(ev.size(), 2u);
    EXPECT_EQ(ev[0].id, "e2") << "the all-day one starts at local midnight";
    EXPECT_EQ(ev[0].start, from);
    EXPECT_EQ(ev[1].title, "Call with Anna");
    EXPECT_NE(google.urls.back().find("singleEvents=true"), std::string::npos);
    EXPECT_TRUE(std::holds_alternative<Calendar::Problem>(
        Calendar::events(google, "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbb0ca1", from, to, "UTC")))
        << "no link, no events";
}

// A busy range comes in pages: every page is read, not the first 250 events only.
TEST_F(CalendarLink, EveryPageOfEventsIsRead) {
    linked();
    google.paged = true;
    const long long from = links.local_instant("2026-10-29", 0, "Asia/Tashkent");
    auto out = Calendar::events(google, kAnna, from, from + 86400, "Asia/Tashkent");
    ASSERT_TRUE(std::holds_alternative<std::vector<Calendar::Event>>(out));
    const auto& ev = std::get<std::vector<Calendar::Event>>(out);
    ASSERT_EQ(ev.size(), 2u);
    EXPECT_EQ(ev[0].id, "e1");
    EXPECT_EQ(ev[1].id, "e3");
    EXPECT_NE(google.urls.back().find("pageToken=p2"), std::string::npos);
}

TEST_F(CalendarLink, DisconnectRemoves) {
    linked();
    EXPECT_TRUE(links.remove(kAnna));
    EXPECT_FALSE(links.get(kAnna).has_value());
    EXPECT_FALSE(links.remove(kAnna));
}
