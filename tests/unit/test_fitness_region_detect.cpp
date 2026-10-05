/**
 * @file test_fitness_region_detect.cpp
 * @brief Region detection: the region of an account is the candidate that
 *        returns data.
 */

#include <string>

#include <gtest/gtest.h>

#include "fitness/FakeHttpTransport.hpp"
#include "fitness/xiaomi/CloudClient.hpp"
#include "fitness/xiaomi/Errors.hpp"
#include "fitness/xiaomi/RegionDetect.hpp"

namespace {

const char* const kEmpty = R"({"code":0,"result":{"data_list":[],"has_more":false}})";
const char* const kWithData = R"({"code":0,"result":{"data_list":[{"a":1}],"has_more":true})";

Xiaomi::CloudClient logged_in(FakeHttpTransport& transport) {
    transport.reply_login();
    Xiaomi::CloudClient client(transport, {"1234567890", std::string(347, 'S'), "cn"}, {});
    client.set_retry_backoff_base_ms(0);
    client.login();
    return client;
}

}  // namespace

TEST(XiaomiRegionDetect, PicksTheRegionThatHasData) {
    FakeHttpTransport transport;
    auto client = logged_in(transport);
    transport.reply_encrypted(kEmpty);     // cn
    transport.reply_encrypted(kWithData);  // sg

    EXPECT_EQ(Xiaomi::detect_region(client, "2026-10-05").value_or(""), "sg");

    // One request per asked region, each to that region's host, and no more
    // after the answer.
    ASSERT_EQ(transport.requests().size(), 4u);
    EXPECT_EQ(transport.requests()[2].url, "https://hlth.io.mi.com/app/v1/data/get_fitness_data_by_time");
    EXPECT_EQ(transport.requests()[3].url, "https://sg.hlth.io.mi.com/app/v1/data/get_fitness_data_by_time");
}

TEST(XiaomiRegionDetect, ReturnsNulloptWhenEveryRegionIsEmpty) {
    FakeHttpTransport transport;
    auto client = logged_in(transport);
    for (std::size_t i = 0; i < Xiaomi::kRegionProbeOrder.size(); ++i) {
        transport.reply_encrypted(kEmpty);
    }
    EXPECT_FALSE(Xiaomi::detect_region(client, "2026-10-05").has_value());
    EXPECT_EQ(transport.requests().size(), 2u + Xiaomi::kRegionProbeOrder.size());
}

TEST(XiaomiRegionDetect, SkipsARegionThatErrors) {
    FakeHttpTransport transport;
    auto client = logged_in(transport);
    transport.reply({404, "", {}});        // cn: not retried, not fatal
    transport.reply_encrypted(kWithData);  // sg
    EXPECT_EQ(Xiaomi::detect_region(client, "2026-10-05").value_or(""), "sg");
}

TEST(XiaomiRegionDetect, AuthRefusalPropagates) {
    FakeHttpTransport transport;
    auto client = logged_in(transport);
    transport.reply({401, "", {}});
    EXPECT_THROW(Xiaomi::detect_region(client, "2026-10-05"), Xiaomi::MiFitnessAuthError);
}

TEST(XiaomiRegionDetect, EveryKnownRegionIsProbedOnce) {
    for (const auto region : Xiaomi::kKnownRegions) {
        int seen = 0;
        for (const auto probed : Xiaomi::kRegionProbeOrder) {
            seen += probed == region ? 1 : 0;
        }
        EXPECT_EQ(seen, 1) << region;
    }
}
