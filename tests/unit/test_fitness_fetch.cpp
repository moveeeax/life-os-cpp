/**
 * @file test_xiaomi_fetch.cpp
 * @brief Region hosts, range bounds, request signing, pagination, envelope.
 */

#include <string>

#include <gtest/gtest.h>

#include "fitness/FakeHttpTransport.hpp"
#include "fitness/xiaomi/CloudClient.hpp"
#include "fitness/xiaomi/Regions.hpp"

namespace {

Xiaomi::Credentials seed() {
    return {"1234567890", std::string(347, 'S'), "cn"};
}

std::string page(const std::string& items, bool has_more, const std::string& next_key) {
    return std::string(R"({"code":0,"result":{"data_list":[)") + items + R"(],"has_more":)" +
           (has_more ? "true" : "false") + R"(,"next_key":)" + next_key + "}}";
}

}  // namespace

TEST(XiaomiRegions, HostMapping) {
    EXPECT_EQ(Xiaomi::host_for_region("cn"), "https://hlth.io.mi.com");
    EXPECT_EQ(Xiaomi::host_for_region(""), "https://hlth.io.mi.com");
    EXPECT_EQ(Xiaomi::host_for_region("de"), "https://de.hlth.io.mi.com");
    EXPECT_EQ(Xiaomi::host_for_region("sg"), "https://sg.hlth.io.mi.com");
    EXPECT_EQ(Xiaomi::kKnownRegions.size(), 6u);
}

// Region cn computes day bounds in UTC+8, the others in UTC. A mistake here
// shifts the day and corrupts daily aggregates, so the numbers are spelled out.
TEST(XiaomiRegions, RangeBoundsUseRegionOffset) {
    // 2026-09-22 00:00:00 +08:00 == 1790006400, end of day == 1790092799.
    const auto cn = Xiaomi::range_to_timestamps("2026-09-22", "2026-09-22", "cn");
    EXPECT_EQ(cn.first, 1790006400);
    EXPECT_EQ(cn.second, 1790092799);
    // 2026-09-22 00:00:00 UTC == 1790035200, exactly eight hours later.
    const auto utc = Xiaomi::range_to_timestamps("2026-09-22", "2026-09-22", "de");
    EXPECT_EQ(utc.first, 1790035200);
    EXPECT_EQ(utc.first - cn.first, 8 * 3600);
}

TEST(XiaomiRegions, RangeRejectsMalformedDates) {
    EXPECT_THROW(Xiaomi::range_to_timestamps("22.09.2026", "2026-09-22", "cn"), Xiaomi::MiFitnessProtocolError);
    EXPECT_THROW(Xiaomi::range_to_timestamps("2026-13-01", "2026-13-02", "cn"), Xiaomi::MiFitnessProtocolError);
    EXPECT_THROW(Xiaomi::range_to_timestamps("2026-02-30", "2026-03-01", "cn"), Xiaomi::MiFitnessProtocolError);
}

TEST(XiaomiFetch, PaginatesUntilHasMoreIsFalse) {
    FakeHttpTransport transport;
    transport.reply_login();
    transport.reply_encrypted(page(R"({"a":1})", true, R"("k1")"));
    transport.reply_encrypted(page(R"({"a":2})", false, "null"));
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    client.login();

    const auto items = client.fetch_key("steps", "2026-09-22", "2026-09-23", std::nullopt);

    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0]["a"], 1);
    EXPECT_EQ(items[1]["a"], 2);
    // The second page carries the first page's cursor. The data field is
    // encrypted, so decrypt it the way the server does: nonce from the body.
    const auto& second_page = transport.requests().back();
    const std::string nonce = Xiaomi::Crypto::b64_decode(FakeHttpTransport::form_value(second_page.body, "_nonce"));
    const std::string signed_nonce = Xiaomi::Crypto::signed_nonce(FakeHttpTransport::kSsecurityB64, nonce);
    const std::string decrypted = Xiaomi::Crypto::rc4(
        signed_nonce, Xiaomi::Crypto::b64_decode(FakeHttpTransport::form_value(second_page.body, "data")));
    EXPECT_NE(decrypted.find(R"("next_key":"k1")"), std::string::npos) << decrypted;
}

TEST(XiaomiFetch, RepeatedCursorIsTreatedAsALoop) {
    FakeHttpTransport transport;
    transport.reply_login();
    transport.reply_encrypted(page(R"({"a":1})", true, R"("same")"));
    transport.reply_encrypted(page(R"({"a":2})", true, R"("same")"));
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    client.login();

    EXPECT_THROW(client.fetch_key("steps", "2026-09-22", "2026-09-23", std::nullopt), Xiaomi::MiFitnessProtocolError);
}

TEST(XiaomiFetch, PageCeilingStopsRunawayPagination) {
    FakeHttpTransport transport;
    transport.reply_login();
    for (int i = 0; i < 12; ++i) {
        transport.reply_encrypted(page(R"({"a":1})", true, R"("k)" + std::to_string(i) + R"(")"));
    }
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    client.login();
    client.set_max_pages(10);

    EXPECT_THROW(client.fetch_key("steps", "2026-01-01", "2026-09-29", std::nullopt), Xiaomi::MiFitnessProtocolError);
    // Login is two requests; no more pages than the cap were fetched.
    EXPECT_LE(transport.requests().size(), 12u);
}

TEST(XiaomiFetch, RequestCarriesAllSignatureFields) {
    FakeHttpTransport transport;
    transport.reply_login();
    transport.reply_encrypted(page("", false, "null"));
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    client.login();
    client.fetch_key("steps", "2026-09-22", "2026-09-22", std::nullopt);

    const auto& request = transport.requests().back();
    EXPECT_EQ(request.method, "POST");
    EXPECT_NE(request.url.find("/app/v1/data/get_fitness_data_by_time"), std::string::npos);
    for (const char* field : {"data", "rc4_hash__", "signature", "_nonce"}) {
        EXPECT_NO_THROW(FakeHttpTransport::form_value(request.body, field)) << field;
    }
    // The payload is encrypted: the plaintext request key is not visible in the body.
    EXPECT_EQ(request.body.find("start_time"), std::string::npos);
}

// Review finding 3: Python accepts has_more as a number (truthiness), and an
// envelope shape mismatch must be a protocol error, not a bare nlohmann
// exception outside the retry taxonomy.
TEST(XiaomiFetch, NumericHasMoreKeepsPaginating) {
    FakeHttpTransport transport;
    transport.reply_login();
    transport.reply_encrypted(R"({"code":0,"result":{"data_list":[{"a":1}],"has_more":1,"next_key":"k1"}})");
    transport.reply_encrypted(R"({"code":0,"result":{"data_list":[{"a":2}],"has_more":0,"next_key":null}})");
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    client.login();
    EXPECT_EQ(client.fetch_key("steps", "2026-09-22", "2026-09-23", std::nullopt).size(), 2u);
}

// An empty next_key ends pagination in Python as falsy. An extra request with
// an empty cursor on a live sync would turn into a false loop error.
TEST(XiaomiFetch, EmptyNextKeyEndsPagination) {
    FakeHttpTransport transport;
    transport.reply_login();
    transport.reply_encrypted(R"({"code":0,"result":{"data_list":[{"a":1}],"has_more":true,"next_key":""}})");
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    client.login();
    const auto items = client.fetch_key("steps", "2026-09-22", "2026-09-23", std::nullopt);
    EXPECT_EQ(items.size(), 1u);
    EXPECT_EQ(transport.requests().size(), 3u);  // login: two, data: one
}

TEST(XiaomiFetch, NonObjectResultIsAProtocolError) {
    FakeHttpTransport transport;
    transport.reply_login();
    transport.reply_encrypted(R"({"code":0,"result":[1,2,3]})");
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    client.login();
    EXPECT_THROW(client.fetch_key("steps", "2026-09-22", "2026-09-22", std::nullopt), Xiaomi::MiFitnessProtocolError);
}

// A non-zero code is a server failure, not an empty result.
TEST(XiaomiFetch, NonZeroCodeIsAProtocolError) {
    FakeHttpTransport transport;
    transport.reply_login();
    transport.reply_encrypted(R"({"code":1234,"message":"server broke"})");
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    client.login();

    EXPECT_THROW(client.fetch_key("steps", "2026-09-22", "2026-09-22", std::nullopt), Xiaomi::MiFitnessProtocolError);
}

// Upstream auth codes: 401, 403, -6, -10001. The job queue does not retry such
// a failure, so the error class must differ.
TEST(XiaomiFetch, AuthCodeIsAnAuthError) {
    FakeHttpTransport transport;
    transport.reply_login();
    transport.reply_encrypted(R"({"code":-10001,"message":"auth expired"})");
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    client.login();

    EXPECT_THROW(client.fetch_key("steps", "2026-09-22", "2026-09-22", std::nullopt), Xiaomi::MiFitnessAuthError);
}

TEST(XiaomiFetch, FetchBeforeLoginIsAnAuthError) {
    FakeHttpTransport transport;
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    EXPECT_THROW(client.fetch_key("steps", "2026-09-22", "2026-09-22", std::nullopt), Xiaomi::MiFitnessAuthError);
    EXPECT_TRUE(transport.requests().empty());
}

// --- Data request retries ----------------------------------------------------
// The live July-September backfill failed on Xiaomi gateway 502/504 and curl
// timeouts: the cloud answers deep ranges unreliably. The Python bridge retried
// such failures up to three attempts with backoff; the port must behave the
// same. Backoff is zero in tests, otherwise every run sleeps for seconds.

TEST(XiaomiRetry, TransientServerErrorIsRetried) {
    FakeHttpTransport transport;
    transport.reply_login();
    transport.reply({502, "Bad Gateway", {}});
    transport.reply_encrypted(page(R"({"a":1})", false, "null"));
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    client.set_retry_backoff_base_ms(0);
    client.login();

    const auto items = client.fetch_key("steps", "2026-09-22", "2026-09-23", std::nullopt);

    ASSERT_EQ(items.size(), 1u);
    // Two login requests, the rejected attempt and its retry.
    EXPECT_EQ(transport.requests().size(), 4u);
}

TEST(XiaomiRetry, TransportFailureIsRetried) {
    FakeHttpTransport transport;
    transport.reply_login();
    transport.reply_transport_error("Timeout was reached");
    transport.reply_encrypted(page(R"({"a":1})", false, "null"));
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    client.set_retry_backoff_base_ms(0);
    client.login();

    const auto items = client.fetch_key("steps", "2026-09-22", "2026-09-23", std::nullopt);

    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(transport.requests().size(), 4u);
}

TEST(XiaomiRetry, GivesUpAfterThreeAttempts) {
    FakeHttpTransport transport;
    transport.reply_login();
    transport.reply({502, "", {}});
    transport.reply({503, "", {}});
    transport.reply({504, "", {}});
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    client.set_retry_backoff_base_ms(0);
    client.login();

    EXPECT_THROW(client.fetch_key("steps", "2026-09-22", "2026-09-23", std::nullopt), Xiaomi::MiFitnessProtocolError);
    // Exactly three attempts, no more: login 2 + data 3.
    EXPECT_EQ(transport.requests().size(), 5u);
}

TEST(XiaomiRetry, AuthRefusalIsNotRetried) {
    FakeHttpTransport transport;
    transport.reply_login();
    transport.reply({401, "", {}});
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    client.set_retry_backoff_base_ms(0);
    client.login();

    EXPECT_THROW(client.fetch_key("steps", "2026-09-22", "2026-09-23", std::nullopt), Xiaomi::MiFitnessAuthError);
    // A retry would burn attempts: a fresh token is needed, not another try.
    EXPECT_EQ(transport.requests().size(), 3u);
}

TEST(XiaomiRetry, EnvelopeErrorCodeIsNotRetried) {
    FakeHttpTransport transport;
    transport.reply_login();
    transport.reply_encrypted(R"({"code":42,"result":{}})");
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    client.set_retry_backoff_base_ms(0);
    client.login();

    EXPECT_THROW(client.fetch_key("steps", "2026-09-22", "2026-09-23", std::nullopt), Xiaomi::MiFitnessProtocolError);
    // The response arrived and decrypted: repeating the same request will not change the code.
    EXPECT_EQ(transport.requests().size(), 3u);
}
