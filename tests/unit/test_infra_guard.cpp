/**
 * @file test_infra_guard.cpp
 * @brief TestHelpers::infra_required: how every infra-dependent fixture reads
 *        CI_REQUIRE_INFRA before it skips or fails.
 */

#include <gtest/gtest.h>

#include "test_helpers.hpp"

TEST(InfraGuard, CiRequireInfraTurnsSkipsIntoFailures) {
    {
        TestHelpers::ScopedEnv unset("CI_REQUIRE_INFRA");
        EXPECT_FALSE(TestHelpers::infra_required()) << "locally a missing sidecar skips";
    }
    for (const char* off : {"", "0", "false"}) {
        TestHelpers::ScopedEnv env("CI_REQUIRE_INFRA", off);
        EXPECT_FALSE(TestHelpers::infra_required()) << "'" << off << "'";
    }
    for (const char* on : {"1", "true", "yes"}) {
        TestHelpers::ScopedEnv env("CI_REQUIRE_INFRA", on);
        EXPECT_TRUE(TestHelpers::infra_required()) << "'" << on << "'";
    }
}
