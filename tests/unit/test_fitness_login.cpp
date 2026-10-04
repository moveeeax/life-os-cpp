/**
 * @file test_xiaomi_login.cpp
 * @brief Two-step login to the Xiaomi cloud on a fake transport.
 *
 * There is no network here and must not be: that is exactly why the transport
 * sits behind an interface. This checks what a live run cannot show: rejection
 * of a foreign host in a redirect, loss of the rotated token and leakage of
 * secrets into the error text.
 */

#include <optional>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "fitness/FakeHttpTransport.hpp"
#include "fitness/xiaomi/CloudClient.hpp"

namespace {

Xiaomi::Credentials seed() {
    return {"1234567890", std::string(347, 'S'), "cn"};
}

std::string login_body(const std::string& token) {
    return std::string("&&&START&&&") + R"({"passToken":")" + token +
           R"(","userId":1234567890,"ssecurity":"c2VjcmV0LW1hdGVyaWFsIQ==",)" +
           R"("location":"https://account.xiaomi.com/pass/end"})";
}

std::string header_value(const Xiaomi::HttpRequest& request, const std::string& name) {
    for (const auto& [key, value] : request.headers) {
        if (key == name) {
            return value;
        }
    }
    return {};
}

}  // namespace

TEST(XiaomiLogin, TwoStepLoginStoresRotatedToken) {
    FakeHttpTransport transport;
    transport.reply_login("ROTATED");
    std::optional<Xiaomi::Credentials> rotated;
    Xiaomi::CloudClient client(transport, seed(), [&](const auto& c) { rotated = c; });

    client.login();

    ASSERT_EQ(transport.requests().size(), 2u);
    EXPECT_NE(transport.requests().at(0).url.find("sid=miothealth"), std::string::npos);
    EXPECT_EQ(transport.requests().at(1).url, "https://account.xiaomi.com/pass/end");
    ASSERT_TRUE(rotated.has_value());
    EXPECT_EQ(rotated->pass_token, "ROTATED");
    EXPECT_EQ(client.credentials().pass_token, "ROTATED");
}

// The header is assembled in exactly this form: order and separator matter.
TEST(XiaomiLogin, SendsUserIdAndTokenAsCookies) {
    FakeHttpTransport transport;
    transport.reply_login();
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});

    client.login();

    EXPECT_EQ(header_value(transport.requests().at(0), "Cookie"),
              "userId=1234567890; passToken=" + std::string(347, 'S'));
}

TEST(XiaomiLogin, MissingStartPrefixIsRejected) {
    FakeHttpTransport transport;
    transport.reply({200, R"({"passToken":"x","userId":1,"ssecurity":"QQ==","location":"https://mi.com/x"})", {}});
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    EXPECT_THROW(client.login(), Xiaomi::MiFitnessAuthError);
}

TEST(XiaomiLogin, MissingRequiredFieldIsRejected) {
    FakeHttpTransport transport;
    transport.reply({200, std::string("&&&START&&&") + R"({"userId":1,"ssecurity":"QQ=="})", {}});
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    EXPECT_THROW(client.login(), Xiaomi::MiFitnessAuthError);
}

TEST(XiaomiLogin, MalformedSsecurityIsRejected) {
    FakeHttpTransport transport;
    transport.reply({200,
                     std::string("&&&START&&&") + R"({"passToken":"x","userId":1,"ssecurity":"not base64!",)"
                                                  R"("location":"https://account.xiaomi.com/pass/end"})",
                     {}});
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    EXPECT_THROW(client.login(), Xiaomi::MiFitnessAuthError);
}

// The rules mirror upstream: https only, Xiaomi domains only, no credentials in
// the URL and no non-standard port. Anything else is a token leak channel.
TEST(XiaomiLogin, RedirectAllowlistFollowsUpstreamRules) {
    EXPECT_TRUE(Xiaomi::is_allowed_login_redirect("https://account.xiaomi.com/pass/end"));
    EXPECT_TRUE(Xiaomi::is_allowed_login_redirect("https://sts.api.io.mi.com/sts"));
    EXPECT_TRUE(Xiaomi::is_allowed_login_redirect("https://mi.com/x"));
    EXPECT_TRUE(Xiaomi::is_allowed_login_redirect("https://ACCOUNT.XIAOMI.COM/x"));
    EXPECT_TRUE(Xiaomi::is_allowed_login_redirect("https://account.xiaomi.com:443/x"));

    EXPECT_FALSE(Xiaomi::is_allowed_login_redirect("http://account.xiaomi.com/x"));
    EXPECT_FALSE(Xiaomi::is_allowed_login_redirect("https://evil.example.com/x"));
    EXPECT_FALSE(Xiaomi::is_allowed_login_redirect("https://xiaomi.com.evil.test/x"));
    EXPECT_FALSE(Xiaomi::is_allowed_login_redirect("https://user:pass@account.xiaomi.com/x"));
    EXPECT_FALSE(Xiaomi::is_allowed_login_redirect("https://account.xiaomi.com:8443/x"));
    EXPECT_FALSE(Xiaomi::is_allowed_login_redirect("https://account.xiaomi.com/\nx"));
    EXPECT_FALSE(Xiaomi::is_allowed_login_redirect(""));
    EXPECT_FALSE(Xiaomi::is_allowed_login_redirect("notaurl"));
}

TEST(XiaomiLogin, ForeignRedirectLocationIsRefused) {
    FakeHttpTransport transport;
    transport.reply({200,
                     std::string("&&&START&&&") +
                         R"({"passToken":"x","userId":1,"ssecurity":"c2VjcmV0LW1hdGVyaWFsIQ==",)"
                         R"("location":"https://evil.example.com/steal"})",
                     {}});
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});

    EXPECT_THROW(client.login(), Xiaomi::MiFitnessAuthError);
    // No request goes to the foreign host at all.
    EXPECT_EQ(transport.requests().size(), 1u);
}

TEST(XiaomiLogin, MissingServiceTokenCookieIsRejected) {
    FakeHttpTransport transport;
    transport.reply({200, login_body("NEWTOKEN"), {}});
    transport.reply({200, "", {{"set-cookie", "somethingElse=1"}}});
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    EXPECT_THROW(client.login(), Xiaomi::MiFitnessAuthError);
}

// Rotation already happened on the Xiaomi side, but there is nowhere to store
// it: the database is down. The session must carry on with the new token,
// otherwise it is lost for good.
TEST(XiaomiLogin, RotationPersistFailureDoesNotLoseTheNewToken) {
    FakeHttpTransport transport;
    transport.reply_login("ROTATED");
    Xiaomi::CloudClient client(
        transport, seed(), [](const auto&) { throw std::runtime_error("postgres unavailable"); });

    EXPECT_NO_THROW(client.login());
    EXPECT_EQ(client.credentials().pass_token, "ROTATED");
}

// The Xiaomi cloud answers a request without User-Agent with an envelope
// carrying passToken:null: confirmed by live diagnostics on 2026-09-29. Both
// upstream clients (httpx in the Python bridge, urllib in the diagnostics) send
// their default UA, libcurl sends none. The header must be on every request.
TEST(XiaomiLogin, EveryRequestCarriesAUserAgent) {
    FakeHttpTransport transport;
    transport.reply_login();
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    client.login();
    for (const auto& request : transport.requests()) {
        EXPECT_FALSE(header_value(request, "User-Agent").empty()) << request.url;
    }
}

// Review finding 1: a transient 5xx from account.xiaomi.com must not look like
// dead credentials - an auth failure is not retried by the queue and sends the
// owner to reseed a live token.
TEST(XiaomiLogin, LoginHttp5xxIsAProtocolError) {
    FakeHttpTransport transport;
    transport.reply({502, "<html>bad gateway</html>", {}});
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    EXPECT_THROW(client.login(), Xiaomi::MiFitnessProtocolError);
}

TEST(XiaomiLogin, LoginHttp401IsAnAuthError) {
    FakeHttpTransport transport;
    transport.reply({401, "", {}});
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    EXPECT_THROW(client.login(), Xiaomi::MiFitnessAuthError);
}

TEST(XiaomiLogin, ErrorTextCarriesNoSecrets) {
    FakeHttpTransport transport;
    transport.reply({200, std::string("&&&START&&&") + R"({"passToken":"SUPERSECRET","userId":1})", {}});
    Xiaomi::CloudClient client(transport, seed(), [](const auto&) {});
    try {
        client.login();
        FAIL() << "expected rejection";
    } catch (const Xiaomi::MiFitnessAuthError& e) {
        const std::string text = e.what();
        EXPECT_EQ(text.find("SUPERSECRET"), std::string::npos);
        EXPECT_EQ(text.find(std::string(20, 'S')), std::string::npos);
    }
}
