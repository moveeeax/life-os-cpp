/**
 * @file test_api_keys.cpp
 * @brief API key lifecycle: create (secret once) → authenticate → list (no
 *        secret) → revoke (stops auth) → owner-scoping.
 */

#include <functional>
#include <string>

#include <drogon/HttpRequest.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/ApiKeyController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "repositories/RoleRepository.hpp"
#include "repositories/UserRepository.hpp"
#include "security/ApiKeys.hpp"
#include "test_helpers.hpp"

using namespace drogon;
using json = nlohmann::json;

namespace {

class ApiKeysTest : public TestHelpers::CoreBackedTest {
protected:
    Api::ApiKeyController controller;

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        // The users CASCADE inside wipe_app_data also clears api_keys
        // (FK ON DELETE CASCADE).
        TestHelpers::wipe_app_data();
    }

    Security::Auth::AuthPrincipal seed_user(const std::string& email, const std::string& role_name = "User") {
        Repositories::RoleRepository roles;
        Repositories::UserRepository users;
        auto role = roles.find_by_name(role_name);
        EXPECT_TRUE(role.has_value());
        auto u = users.create(email, std::string("$argon2id$x"), std::nullopt, std::nullopt, role->id, true);
        Security::Auth::AuthPrincipal p;
        p.subject = u.id;
        p.raw_claims = json{{"sub", u.id}, {"permissions", role ? role->permissions : 0u}};
        return p;
    }

    json create_key(const Security::Auth::AuthPrincipal& p, const std::string& name, int* status = nullptr) {
        HttpResponsePtr resp;
        controller.create(TestHelpers::authed_json(p, {{"name", name}}), [&](const HttpResponsePtr& r) { resp = r; });
        if (status)
            *status = resp->statusCode();
        return json::parse(std::string(resp->body()));
    }

    static HttpRequestPtr with_key(const std::string& key) {
        auto req = HttpRequest::newHttpRequest();
        req->addHeader("X-API-Key", key);
        return req;
    }
};

TEST_F(ApiKeysTest, CreateReturnsSecretOnceThenAuthenticates) {
    auto user = seed_user("keyuser@example.com");
    int status = 0;
    auto body = create_key(user, "ci-token", &status);
    ASSERT_EQ(status, k201Created);

    const std::string key = body["key"];
    EXPECT_EQ(key.rfind("cpk_", 0), 0u);  // has the prefix
    EXPECT_EQ(body["prefix"].get<std::string>().rfind("cpk_", 0), 0u);
    EXPECT_FALSE(body.contains("key_hash"));  // never leaks the hash

    auto principal = Security::ApiKeys::authenticate(with_key(key));
    ASSERT_TRUE(principal.has_value());
    EXPECT_EQ(principal->subject, user.subject);
}

TEST_F(ApiKeysTest, ListHidesSecretAndRevokeStopsAuth) {
    auto user = seed_user("keyuser2@example.com");
    auto created = create_key(user, "t");
    const std::string key = created["key"];
    const std::string id = created["id"];

    HttpResponsePtr listResp;
    controller.list(TestHelpers::authed(user), [&](const HttpResponsePtr& r) { listResp = r; });
    auto list = json::parse(std::string(listResp->body()));
    ASSERT_EQ(list["total"], 1);
    EXPECT_FALSE(list["data"][0].contains("key"));
    EXPECT_FALSE(list["data"][0].contains("key_hash"));

    HttpResponsePtr delResp;
    controller.remove(
        TestHelpers::authed(user), [&](const HttpResponsePtr& r) { delResp = r; }, id);
    EXPECT_EQ(delResp->statusCode(), k200OK);

    EXPECT_FALSE(Security::ApiKeys::authenticate(with_key(key)).has_value());  // revoked
}

TEST_F(ApiKeysTest, AdminKeyCarriesNoAdminBitAndIsMarkedAsAKey) {
    auto admin = seed_user("key-admin@example.com", "Administrator");
    const std::string key = create_key(admin, "admin-script")["key"];

    auto principal = Security::ApiKeys::authenticate(with_key(key));
    ASSERT_TRUE(principal.has_value());
    EXPECT_TRUE(principal->via_api_key);
    EXPECT_EQ(principal->raw_claims.value("api_key", false), true);
    const auto perms = principal->raw_claims["permissions"].get<std::int64_t>();
    EXPECT_EQ(perms & Security::Auth::kAdminPermissionBits, 0);
    // The Administrator role is the sentinel bit alone, so the key must still
    // carry the data bits the sentinel implied, or it reads nothing.
    EXPECT_EQ(perms & Domain::Permission::kGeneral, Domain::Permission::kGeneral);
    EXPECT_EQ(perms & Domain::Permission::kFitnessRead, Domain::Permission::kFitnessRead);
    EXPECT_EQ(perms & Domain::Permission::kFitnessSync, Domain::Permission::kFitnessSync);
    EXPECT_EQ(perms & Domain::Permission::kAuditRead, 0);

    // The admin guard sees the masked bitmask and refuses.
    auto req = TestHelpers::authed(*principal);
    EXPECT_NE(Security::Auth::require_admin(req), nullptr);
}

TEST_F(ApiKeysTest, KeyPrincipalCannotManageKeysEvenPastTheMiddleware) {
    // The controller refuses a key principal itself, whatever path spelling
    // got the request routed here.
    auto user = seed_user("key-self@example.com");
    const std::string key = create_key(user, "k")["key"];
    auto principal = Security::ApiKeys::authenticate(with_key(key));
    ASSERT_TRUE(principal.has_value());

    HttpResponsePtr resp;
    controller.create(TestHelpers::authed_json(*principal, {{"name", "more"}}),
                      [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k401Unauthorized);
    EXPECT_EQ(json::parse(std::string(resp->body()))["error"], "key_not_allowed");

    HttpResponsePtr listed;
    controller.list(TestHelpers::authed(*principal), [&](const HttpResponsePtr& r) { listed = r; });
    ASSERT_NE(listed, nullptr);
    EXPECT_EQ(listed->statusCode(), k401Unauthorized);
}

TEST_F(ApiKeysTest, RevokeIsOwnerScoped) {
    auto alice = seed_user("alice@example.com");
    auto bob = seed_user("bob@example.com");
    const std::string id = create_key(alice, "a")["id"];

    // Bob can't revoke Alice's key → 404 (no info leak that it exists).
    HttpResponsePtr resp;
    controller.remove(
        TestHelpers::authed(bob), [&](const HttpResponsePtr& r) { resp = r; }, id);
    EXPECT_EQ(resp->statusCode(), k404NotFound);
}

}  // namespace
