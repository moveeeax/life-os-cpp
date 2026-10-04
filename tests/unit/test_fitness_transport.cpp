/**
 * @file test_fitness_transport.cpp
 * @brief The HTTP request timeout to the cloud is read from the config.
 *
 * The live July-September backfill failed with "Timeout was reached": the
 * hard-coded 20 seconds of CurlTransport is less than the cloud's response time
 * on deep ranges. The fitness.xiaomi.http_timeout_seconds knob
 * (MI_FITNESS_HTTP_TIMEOUT) raises the limit without a rebuild; the default
 * stays the same.
 */

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "fitness/xiaomi/CurlTransport.hpp"
#include "fitness/xiaomi/Service.hpp"
#include "test_helpers.hpp"
#include "utils/Config.hpp"

namespace {

constexpr const char* kConfigFile = "xiaomi_timeout_config.json";

}  // namespace

class XiaomiTransportTimeoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        unsetenv("MI_FITNESS_HTTP_TIMEOUT");
        if (Config::is_initialized()) {
            Config::shutdown();
        }
    }

    void TearDown() override {
        if (Config::is_initialized()) {
            Config::shutdown();
        }
        std::filesystem::remove(kConfigFile);
        TestHelpers::reset_all_globals();
    }
};

TEST_F(XiaomiTransportTimeoutTest, CurlTransportCarriesExplicitTimeout) {
    Xiaomi::CurlTransport transport(120);
    EXPECT_EQ(transport.timeout_seconds(), 120);
}

TEST_F(XiaomiTransportTimeoutTest, DefaultsToTwentySecondsWithoutConfig) {
    ASSERT_FALSE(Config::is_initialized());
    EXPECT_EQ(Xiaomi::Service::http_timeout_seconds(), 20);
}

TEST_F(XiaomiTransportTimeoutTest, ReadsTimeoutFromConfig) {
    std::ofstream(kConfigFile) << R"({"fitness": {"xiaomi": {"http_timeout_seconds": 120}}})";
    Config::initialize(kConfigFile);
    EXPECT_EQ(Xiaomi::Service::http_timeout_seconds(), 120);
}
