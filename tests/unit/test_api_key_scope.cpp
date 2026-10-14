/**
 * @file test_api_key_scope.cpp
 * @brief Which paths an API key may authenticate: everything except the
 *        account and admin surface (the keys themselves live under
 *        /api/v1/account/api-keys).
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "security/ApiKeys.hpp"

namespace {

using Security::ApiKeys::path_allows_key;

TEST(ApiKeyScope, RefusesAccountAdminAndKeyManagement) {
    EXPECT_FALSE(path_allows_key("/api/v1/account/api-keys"));
    EXPECT_FALSE(path_allows_key("/api/v1/account/api-keys/123"));
    EXPECT_FALSE(path_allows_key("/api/v1/account/change-password"));
    EXPECT_FALSE(path_allows_key("/api/v1/account"));
    EXPECT_FALSE(path_allows_key("/api/v1/admin/users"));
    EXPECT_FALSE(path_allows_key("/api/v1/admin"));
    EXPECT_FALSE(path_allows_key("/api/v1/api-keys"));
    EXPECT_FALSE(path_allows_key("/api/v1/api-keys/abc"));
}

TEST(ApiKeyScope, RefusesOtherSpellingsOfTheSamePaths) {
    // Case, doubled slashes, dot segments and percent-encoding must not slip
    // a key past the check, whatever the router makes of them.
    for (const char* p : {"/API/v1/admin/users",
                          "/api/V1/Admin/users",
                          "//api/v1/admin/users",
                          "/api//v1//account/api-keys",
                          "/api/v1/./admin/users",
                          "/api/v1/x/../admin/users",
                          "/api/v1/tasks/../../v1/account/change-password",
                          "/api/v1/%61dmin/users",
                          "/api/v1/account%2Fapi-keys",
                          "/api/v1\\admin\\users",
                          "/api/v1/ACCOUNT/API-KEYS",
                          "/api/v1/admin/",
                          "/../api/v1/admin"}) {
        EXPECT_FALSE(path_allows_key(p)) << p;
    }
}

TEST(ApiKeyScope, CanonicalSegments) {
    using Security::ApiKeys::detail::canonical_segments;
    const std::vector<std::string> want{"api", "v1", "admin", "users"};
    EXPECT_EQ(canonical_segments("/API//v1/./x/../Admin/users"), want);
    EXPECT_TRUE(canonical_segments("/").empty());
    EXPECT_TRUE(canonical_segments("/../..").empty());
}

TEST(ApiKeyScope, AllowsTheDataRoutes) {
    EXPECT_TRUE(path_allows_key("/api/v1/tasks"));
    EXPECT_TRUE(path_allows_key("/api/v1/money/transactions"));
    EXPECT_TRUE(path_allows_key("/api/v1/auth/me"));
    // A prefix match is by whole segment: "/api/v1/accounts" is not under
    // "/api/v1/account".
    EXPECT_TRUE(path_allows_key("/api/v1/accounts"));
    EXPECT_TRUE(path_allows_key("/api/v1/administrivia"));
}

}  // namespace
