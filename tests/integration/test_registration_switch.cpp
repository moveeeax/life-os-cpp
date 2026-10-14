/**
 * @file test_registration_switch.cpp
 * @brief auth.registration_open: with the switch off, POST /auth/register
 *        answers 403 registration_closed and the probe reports closed, while
 *        an admin invite still turns into a working account.
 */

#include <chrono>
#include <string>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/AccountController.hpp"
#include "api/AdminController.hpp"
#include "api/AuthController.hpp"
#include "repositories/RoleRepository.hpp"
#include "repositories/UserRepository.hpp"
#include "security/Auth.hpp"
#include "security/Tokens.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kSecret = "test-jwt-secret-for-registration-switch";

class RegistrationClosedTest : public TestHelpers::CoreBackedTest {
protected:
    Api::AuthController auth;
    Api::AdminController admin;
    Api::AccountController account;

    std::string config_file_name() const override { return "registration_closed_test_config.json"; }

    void config_overrides(json& cfg) override {
        cfg["auth"]["mode"] = "jwt";
        cfg["auth"]["jwt"]["secret"] = kSecret;
        cfg["auth"]["cookies"]["enabled"] = true;
        cfg["auth"]["cookies"]["secure"] = false;
        cfg["auth"]["registration_open"] = false;
        cfg["mail"]["enabled"] = false;
        cfg["database"]["migrations_enabled"] = true;
        cfg["database"]["migrations_dir"] = "migrations";
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        TestHelpers::wipe_app_data();
    }

    Security::Auth::AuthPrincipal seed_admin() {
        Repositories::RoleRepository roles;
        Repositories::UserRepository users;
        auto role = roles.find_by_name("Administrator");
        if (!role)
            throw std::runtime_error("Administrator role missing");
        auto u =
            users.create("admin@example.com", std::string("$argon2id$x"), std::nullopt, std::nullopt, role->id, true);
        Security::Auth::AuthPrincipal p;
        p.subject = u.id;
        p.raw_claims = json{{"sub", u.id}, {"permissions", role->permissions}};
        return p;
    }
};

TEST_F(RegistrationClosedTest, RegisterAnswers403WhenClosed) {
    HttpResponsePtr resp;
    auth.registerUser(TestHelpers::post_json({{"email", "walk-in@example.com"}, {"password", "password-123"}}),
                      [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k403Forbidden);
    auto body = json::parse(std::string(resp->body()));
    EXPECT_EQ(body["error"].get<std::string>(), "registration_closed");
    EXPECT_FALSE(Repositories::UserRepository().find_by_email("walk-in@example.com").has_value());
}

TEST_F(RegistrationClosedTest, ProbeReportsClosed) {
    HttpResponsePtr resp;
    auth.registration(TestHelpers::make_request(Get), [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k200OK);
    auto body = json::parse(std::string(resp->body()));
    EXPECT_EQ(body["data"]["open"], false);
}

TEST_F(RegistrationClosedTest, InviteStillWorksWhenClosed) {
    const auto admin_p = seed_admin();
    HttpResponsePtr invited;
    admin.inviteUser(TestHelpers::authed_json(admin_p, {{"email", "invitee@example.com"}}),
                     [&](const HttpResponsePtr& r) { invited = r; });
    ASSERT_NE(invited, nullptr);
    ASSERT_EQ(invited->statusCode(), k201Created) << invited->body();
    auto user = Repositories::UserRepository().find_by_email("invitee@example.com");
    ASSERT_TRUE(user.has_value());

    // The link the invite email carries: an Invite-purpose token for the user.
    const auto token =
        Security::Tokens::issue(kSecret, user->id, Security::Tokens::Purpose::Invite, std::chrono::hours(1));
    HttpResponsePtr joined;
    account.joinFromInvite(
        TestHelpers::post_json({{"new_password", "invited-pass-1"}}),
        [&](const HttpResponsePtr& r) { joined = r; },
        token);
    ASSERT_NE(joined, nullptr);
    EXPECT_EQ(joined->statusCode(), k200OK) << joined->body();

    HttpResponsePtr login;
    auth.login(TestHelpers::post_json({{"email", "invitee@example.com"}, {"password", "invited-pass-1"}}),
               [&](const HttpResponsePtr& r) { login = r; });
    ASSERT_NE(login, nullptr);
    EXPECT_EQ(login->statusCode(), k200OK) << login->body();
}

}  // namespace
