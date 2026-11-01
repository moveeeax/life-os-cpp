/**
 * @file test_auth_flow.cpp
 * @brief Integration tests for the auth flow.
 *
 * Drives the controller methods directly (no real HTTP listener) but
 * with full subsystem init — Postgres for users/roles, Redis for refresh
 * revocation. Skips when those aren't reachable.
 *
 * Coverage:
 *   - register → 201 + user row created (unconfirmed)
 *   - register again with same email → 409
 *   - login bad password → 401 generic
 *   - login good password → 200 + Set-Cookie
 *   - me with valid principal → 200 + user
 *   - refresh path-only smoke (cookie wiring covered by middleware tests
 *     in stage 5; here we exercise the controller's own logic via the
 *     refresh cookie helper)
 */

#include <filesystem>
#include <fstream>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/AuthController.hpp"
#include "database/Database.hpp"
#include "repositories/UserRepository.hpp"
#include "security/Jwt.hpp"
#include "security/Password.hpp"
#include "security/SessionStore.hpp"
#include "test_helpers.hpp"
#include "utils/Time.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kSecret = "test-jwt-secret-for-auth-flow-padding";

class AuthFlowTest : public TestHelpers::CoreBackedTest {
protected:
    Api::AuthController controller;

    std::string config_file_name() const override { return "auth_flow_test_config.json"; }

    void config_overrides(json& cfg) override {
        cfg["auth"]["mode"] = "jwt";
        cfg["auth"]["jwt"]["secret"] = kSecret;
        cfg["auth"]["cookies"]["enabled"] = true;
        // Local dev — no https in the test container.
        cfg["auth"]["cookies"]["secure"] = false;
        cfg["database"]["migrations_enabled"] = true;
        // Point at a dedicated migrations dir so we run 001_users_and_roles.
        cfg["database"]["migrations_dir"] = "migrations";
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        // Wipe users between tests so create() / register() don't conflict.
        TestHelpers::truncate_users();
    }

    static HttpRequestPtr make_post(const json& body) { return TestHelpers::post_json(body); }
};

TEST_F(AuthFlowTest, registerCreatesUnconfirmedUser) {
    auto req = make_post({{"email", "alice@example.com"}, {"password", "correct horse 1"}});
    HttpResponsePtr resp;
    controller.registerUser(req, [&](const HttpResponsePtr& r) { resp = r; });

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k201Created);

    auto body = json::parse(std::string(resp->body()));
    ASSERT_TRUE(body.contains("user"));
    EXPECT_EQ(body["user"]["email"].get<std::string>(), "alice@example.com");
    EXPECT_EQ(body["user"]["confirmed"].get<bool>(), false);
    ASSERT_TRUE(body["user"].contains("role"));

    // Verify the row really landed.
    Repositories::UserRepository repo;
    auto found = repo.find_by_email("alice@example.com");
    ASSERT_TRUE(found.has_value());
    EXPECT_FALSE(found->confirmed);
    EXPECT_TRUE(found->password_hash.has_value());
    EXPECT_TRUE(Security::Password::looks_hashed(*found->password_hash));
}

TEST_F(AuthFlowTest, registerSameEmailTwiceConflicts) {
    auto first = make_post({{"email", "bob@example.com"}, {"password", "12345678"}});
    HttpResponsePtr r1, r2;
    controller.registerUser(first, [&](const HttpResponsePtr& r) { r1 = r; });
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(r1->statusCode(), k201Created);

    // Password must pass the 8-char minimum so the request reaches the
    // duplicate-email check instead of dying at validation.
    auto second = make_post({{"email", "bob@example.com"}, {"password", "another-pass"}});
    controller.registerUser(second, [&](const HttpResponsePtr& r) { r2 = r; });
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(r2->statusCode(), k409Conflict);
    auto body = json::parse(std::string(r2->body()));
    EXPECT_EQ(body["error"].get<std::string>(), "email_taken");
}

TEST_F(AuthFlowTest, registerRejectsShortPassword) {
    auto req = make_post({{"email", "shorty@example.com"}, {"password", "abc"}});
    HttpResponsePtr resp;
    controller.registerUser(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
}

TEST_F(AuthFlowTest, registerRefusesAMegabyteEmailWith400) {
    // Before the length check this string went straight into std::regex_match
    // on an unauthenticated route.
    const std::string huge = std::string(1024 * 1024, 'a') + "@example.com";
    HttpResponsePtr resp;
    controller.registerUser(make_post({{"email", huge}, {"password", "long-enough-1"}}),
                            [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
    auto body = json::parse(std::string(resp->body()));
    ASSERT_TRUE(body.contains("errors"));
    bool too_long = false;
    for (const auto& e : body["errors"])
        too_long = too_long || (e["field"] == "email" && e["code"] == "too_long");
    EXPECT_TRUE(too_long) << body.dump().substr(0, 400);
}

TEST_F(AuthFlowTest, loginWrongPasswordReturns401Generic) {
    // First register.
    auto reg = make_post({{"email", "carol@example.com"}, {"password", "rightpassword"}});
    HttpResponsePtr regr;
    controller.registerUser(reg, [&](const HttpResponsePtr& r) { regr = r; });
    ASSERT_EQ(regr->statusCode(), k201Created);

    // Wrong password.
    auto bad = make_post({{"email", "carol@example.com"}, {"password", "WRONGpassword"}});
    HttpResponsePtr badr;
    controller.login(bad, [&](const HttpResponsePtr& r) { badr = r; });
    ASSERT_EQ(badr->statusCode(), k401Unauthorized);
    auto body = json::parse(std::string(badr->body()));
    // Generic code — no "user_not_found" leakage.
    EXPECT_EQ(body["error"].get<std::string>(), "invalid_credentials");

    // The failed attempt is recorded in the audit trail so brute-force is
    // visible (previously only successful admin actions were audited).
    long audited = Database::get().execute_read([&](auto& txn) {
        auto r = txn.exec_params(
            "SELECT COUNT(*) FROM audit_log WHERE action = 'auth.login_failed' AND details->>'email' = $1",
            "carol@example.com");
        return r.at(0).at(0).template as<long>();
    });
    EXPECT_EQ(audited, 1);
}

TEST_F(AuthFlowTest, loginSucceedsAndSetsCookies) {
    auto reg = make_post({{"email", "dan@example.com"}, {"password", "rightpassword"}});
    HttpResponsePtr regr;
    controller.registerUser(reg, [&](const HttpResponsePtr& r) { regr = r; });
    ASSERT_EQ(regr->statusCode(), k201Created);

    auto good = make_post({{"email", "dan@example.com"}, {"password", "rightpassword"}});
    HttpResponsePtr resp;
    controller.login(good, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k200OK);

    // Session cookies are attached via addCookie() (so Drogon serializes one
    // Set-Cookie line per cookie); they live in the response's cookie map,
    // not the header map, until the response is rendered on the wire.
    const auto& cookies = resp->cookies();
    EXPECT_GE(cookies.size(), 2u);  // access + refresh
    for (const auto& [name, c] : cookies) {
        EXPECT_TRUE(c.isHttpOnly()) << "cookie " << name << " must be HttpOnly";
        EXPECT_EQ(c.sameSite(), drogon::Cookie::SameSite::kLax) << "cookie " << name << " must be SameSite=Lax";
    }
}

TEST_F(AuthFlowTest, loginUnknownEmailReturns401Generic) {
    auto req = make_post({{"email", "noone@example.com"}, {"password", "anything"}});
    HttpResponsePtr resp;
    controller.login(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k401Unauthorized);
    auto body = json::parse(std::string(resp->body()));
    EXPECT_EQ(body["error"].get<std::string>(), "invalid_credentials");
}

TEST_F(AuthFlowTest, meReturnsUserForValidPrincipal) {
    // Create a user, then synthesize a principal in req->attributes(),
    // mirroring what the auth middleware would have done after verifying
    // an access cookie.
    auto reg = make_post({{"email", "eve@example.com"}, {"password", "rightpassword"}});
    HttpResponsePtr regr;
    controller.registerUser(reg, [&](const HttpResponsePtr& r) { regr = r; });
    ASSERT_EQ(regr->statusCode(), k201Created);

    Repositories::UserRepository repo;
    auto user = repo.find_by_email("eve@example.com");
    ASSERT_TRUE(user.has_value());

    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Get);
    Security::Auth::AuthPrincipal principal;
    principal.subject = user->id;
    req->attributes()->insert(Security::Auth::kPrincipalAttr, principal);

    HttpResponsePtr resp;
    controller.me(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k200OK);
    auto body = json::parse(std::string(resp->body()));
    EXPECT_EQ(body["user"]["email"].get<std::string>(), "eve@example.com");
}

class AuthModulesTest : public AuthFlowTest {
protected:
    std::string config_file_name() const override { return "auth_modules_test_config.json"; }
    void config_overrides(json& cfg) override {
        AuthFlowTest::config_overrides(cfg);
        cfg["money"]["enabled"] = true;
        cfg["travel"]["enabled"] = false;
    }
};

TEST_F(AuthModulesTest, modulesAnswersTheSwitchesTheRoutesObey) {
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Get);
    Security::Auth::AuthPrincipal principal;
    principal.subject = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaa01";
    req->attributes()->insert(Security::Auth::kPrincipalAttr, principal);
    HttpResponsePtr resp;
    controller.modules(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    ASSERT_EQ(resp->statusCode(), k200OK) << std::string(resp->body());
    TestHelpers::expect_matches_schema(resp, "GET", "/api/v1/modules");
    const auto data = json::parse(std::string(resp->body()))["data"];
    EXPECT_EQ(data["money"], true);
    EXPECT_EQ(data["travel"], false);
    EXPECT_TRUE(data.contains("medical"));
    EXPECT_TRUE(data.contains("practice"));
    EXPECT_TRUE(data.contains("github"));
    EXPECT_EQ(data.size(), 14U) << "one flag per module";

    auto anonymous = HttpRequest::newHttpRequest();
    anonymous->setMethod(Get);
    HttpResponsePtr refused;
    controller.modules(anonymous, [&](const HttpResponsePtr& r) { refused = r; });
    ASSERT_NE(refused, nullptr);
    EXPECT_EQ(refused->statusCode(), k401Unauthorized);
}

TEST_F(AuthFlowTest, meRefuses401WhenNoPrincipal) {
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Get);
    HttpResponsePtr resp;
    controller.me(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k401Unauthorized);
}

TEST_F(AuthFlowTest, refreshRefusesAnExpiredTokenEvenWithALiveJti) {
    auto reg = make_post({{"email", "frank@example.com"}, {"password", "rightpassword"}});
    HttpResponsePtr regr;
    controller.registerUser(reg, [&](const HttpResponsePtr& r) { regr = r; });
    ASSERT_EQ(regr->statusCode(), k201Created);
    Repositories::UserRepository repo;
    auto user = repo.find_by_email("frank@example.com");
    ASSERT_TRUE(user.has_value());

    // A signed refresh token that expired an hour ago, whose JTI marker is
    // still in Redis: the marker alone must not keep the session alive.
    const auto& cfg = Security::Auth::get().config();
    const auto now = Utils::Time::now_epoch_seconds();
    const std::string jti = "expired-refresh-jti-1";
    const json claims = {{"sub", user->id}, {"iat", now - 7200}, {"exp", now - 3600}, {"typ", "refresh"}, {"jti", jti}};
    const auto token = Security::Auth::issue_hs256_jwt(claims, kSecret);
    ASSERT_TRUE(Security::Sessions::record(cfg.cookies, user->id, jti, 600));

    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->addCookie(cfg.cookies.refresh_name, token);
    HttpResponsePtr resp;
    controller.refresh(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k401Unauthorized);
    auto body = json::parse(std::string(resp->body()));
    EXPECT_EQ(body["error"].get<std::string>(), "token_expired");
}

}  // namespace
