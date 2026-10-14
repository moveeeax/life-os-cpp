/**
 * @file test_api_key_scope.cpp
 * @brief Which paths an API key may authenticate: everything except the
 *        account and admin surface (the keys themselves live under
 *        /api/v1/account/api-keys).
 */

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
