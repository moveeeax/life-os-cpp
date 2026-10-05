/**
 * @file test_fitness_qr_login.cpp
 * @brief Xiaomi QR sign-in over a scripted transport: what is asked, what is
 *        kept server side, and which answers end the attempt.
 */

#include <string>

#include <gtest/gtest.h>

#include "fitness/FakeHttpTransport.hpp"
#include "fitness/xiaomi/Errors.hpp"
#include "fitness/xiaomi/QrLogin.hpp"

namespace {

const std::string kPrefix = "&&&START&&&";

std::string service_login(const std::string& callback = "https://sts-hlth.io.mi.com/healthapp/sts") {
    return kPrefix + R"({"code":70016,"callback":")" + callback + R"(","sid":"miothealth"})";
}

std::string issued(const std::string& qr = "https://account.xiaomi.com/longPolling/qr?ticket=t",
                   const std::string& lp = "https://sgp.account.xiaomi.com/longPolling/lp?ticket=t",
                   const std::string& extra = R"("timeout":300)") {
    return kPrefix + R"({"code":0,"qr":")" + qr +
           R"(","loginUrl":"https://sgp.account.xiaomi.com/longPolling/login?ticket=t&amp;dc=sgp&amp;sid=miothealth","lp":")" +
           lp + R"(",)" + extra + "}";
}

/// Queue the three answers of a successful start.
void reply_start(FakeHttpTransport& transport) {
    transport.reply({200, service_login(), {{"Set-Cookie", "deviceId=dev1; Path=/; Domain=account.xiaomi.com"}}});
    transport.reply({200, issued(), {{"set-cookie", "pass_ua=web; Path=/"}}});
    transport.reply({200, "PNGBYTES", {}});
}

std::string confirmation(const std::string& location = "https://sts-hlth.io.mi.com/healthapp/sts?d=1",
                         const std::string& token = "V1:fresh-token") {
    return kPrefix + R"({"code":0,"userId":4231000052,"passToken":")" + token +
           R"(","ssecurity":"c2VjcmV0","location":")" + location + R"("})";
}

Xiaomi::QrLogin::Attempt started(FakeHttpTransport& transport) {
    reply_start(transport);
    return Xiaomi::QrLogin::start(transport);
}

std::string header(const Xiaomi::HttpRequest& request, const std::string& name) {
    for (const auto& [key, value] : request.headers) {
        if (key == name) {
            return value;
        }
    }
    return {};
}

}  // namespace

TEST(XiaomiQrLogin, StartReturnsTheQrAndKeepsThePollUrl) {
    FakeHttpTransport transport;
    const auto attempt = started(transport);

    EXPECT_EQ(attempt.qr_png, "PNGBYTES");
    EXPECT_EQ(attempt.poll_url, "https://sgp.account.xiaomi.com/longPolling/lp?ticket=t");
    EXPECT_EQ(attempt.timeout_seconds, 300);
    EXPECT_EQ(attempt.cookies, "deviceId=dev1; pass_ua=web");

    ASSERT_EQ(transport.requests().size(), 3u);
    EXPECT_EQ(transport.requests()[0].url, "https://account.xiaomi.com/pass/serviceLogin?_json=true&sid=miothealth");
    const std::string& issue_url = transport.requests()[1].url;
    EXPECT_EQ(issue_url.rfind("https://account.xiaomi.com/longPolling/loginUrl?", 0), 0u);
    EXPECT_NE(issue_url.find("sid=miothealth"), std::string::npos);
    EXPECT_NE(issue_url.find("callback=https%3A%2F%2Fsts-hlth.io.mi.com%2Fhealthapp%2Fsts"), std::string::npos);
    // The service query travels double-encoded, as the sign-in page sends it.
    EXPECT_NE(issue_url.find("qs=%253Fsid%253Dmiothealth%2526_json%253Dtrue"), std::string::npos);
    EXPECT_EQ(transport.requests()[2].url, "https://account.xiaomi.com/longPolling/qr?ticket=t");
    EXPECT_EQ(header(transport.requests()[2], "Cookie"), "deviceId=dev1; pass_ua=web");
}

TEST(XiaomiQrLogin, ConfirmUrlIsUnescaped) {
    FakeHttpTransport transport;
    EXPECT_EQ(started(transport).confirm_url,
              "https://sgp.account.xiaomi.com/longPolling/login?ticket=t&dc=sgp&sid=miothealth");
}

TEST(XiaomiQrLogin, StartRejectsAQrHostOutsideXiaomi) {
    FakeHttpTransport transport;
    transport.reply({200, service_login(), {}});
    transport.reply({200, issued("https://evil.example/qr.png"), {}});
    EXPECT_THROW(Xiaomi::QrLogin::start(transport), Xiaomi::MiFitnessAuthError);
    EXPECT_EQ(transport.requests().size(), 2u) << "the foreign host must not be called";
}

TEST(XiaomiQrLogin, StartRejectsAPollHostOutsideXiaomi) {
    FakeHttpTransport transport;
    transport.reply({200, service_login(), {}});
    transport.reply({200, issued("https://account.xiaomi.com/qr", "https://account.xiaomi.com.evil.example/lp"), {}});
    EXPECT_THROW(Xiaomi::QrLogin::start(transport), Xiaomi::MiFitnessAuthError);
}

TEST(XiaomiQrLogin, StartRejectsACallbackOutsideXiaomi) {
    FakeHttpTransport transport;
    transport.reply({200, service_login("https://evil.example/sts"), {}});
    EXPECT_THROW(Xiaomi::QrLogin::start(transport), Xiaomi::MiFitnessAuthError);
    EXPECT_EQ(transport.requests().size(), 1u);
}

TEST(XiaomiQrLogin, StartFailsWhenXiaomiIssuesNoQr) {
    {
        FakeHttpTransport transport;
        transport.reply({200, service_login(), {}});
        transport.reply({200, kPrefix + R"({"code":70016,"desc":"no"})", {}});
        EXPECT_THROW(Xiaomi::QrLogin::start(transport), Xiaomi::MiFitnessProtocolError);
    }
    {
        FakeHttpTransport transport;
        transport.reply({200, service_login(), {}});
        transport.reply({200, issued("https://account.xiaomi.com/qr", "https://account.xiaomi.com/lp", R"("timeout":0)"),
                         {}});
        EXPECT_THROW(Xiaomi::QrLogin::start(transport), Xiaomi::MiFitnessProtocolError);
    }
    {
        FakeHttpTransport transport;
        transport.reply({503, "", {}});
        EXPECT_THROW(Xiaomi::QrLogin::start(transport), Xiaomi::MiFitnessProtocolError);
    }
    {
        FakeHttpTransport transport;
        transport.reply({200, "<html>maintenance</html>", {}});
        EXPECT_THROW(Xiaomi::QrLogin::start(transport), Xiaomi::MiFitnessProtocolError);
    }
}

TEST(XiaomiQrLogin, PollTimeoutMeansNotYet) {
    FakeHttpTransport transport;
    const auto attempt = started(transport);
    transport.reply_timeout();

    EXPECT_FALSE(Xiaomi::QrLogin::poll(transport, attempt, 3).has_value());

    const auto& request = transport.requests().back();
    EXPECT_EQ(request.url, attempt.poll_url);
    EXPECT_EQ(request.timeout_seconds, 3);
    EXPECT_EQ(header(request, "Cookie"), "deviceId=dev1; pass_ua=web");
}

TEST(XiaomiQrLogin, PollWithoutAResultMeansNotYet) {
    FakeHttpTransport transport;
    const auto attempt = started(transport);
    transport.reply({504, "", {}});
    EXPECT_FALSE(Xiaomi::QrLogin::poll(transport, attempt).has_value());
}

TEST(XiaomiQrLogin, PollReturnsTheConfirmedAccount) {
    FakeHttpTransport transport;
    const auto attempt = started(transport);
    transport.reply({200, confirmation(), {}});

    const auto confirmed = Xiaomi::QrLogin::poll(transport, attempt);
    ASSERT_TRUE(confirmed.has_value());
    EXPECT_EQ(confirmed->user_id, "4231000052");
    EXPECT_EQ(confirmed->pass_token, "V1:fresh-token");
}

TEST(XiaomiQrLogin, PollRejectsARedirectOutsideXiaomi) {
    FakeHttpTransport transport;
    const auto attempt = started(transport);
    transport.reply({200, confirmation("https://evil.example/sts"), {}});
    EXPECT_THROW(Xiaomi::QrLogin::poll(transport, attempt), Xiaomi::MiFitnessAuthError);
}

TEST(XiaomiQrLogin, PollRejectsATokenThatCannotGoIntoACookie) {
    FakeHttpTransport transport;
    const auto attempt = started(transport);
    transport.reply({200, confirmation("https://sts-hlth.io.mi.com/x", "bad; token"), {}});
    EXPECT_THROW(Xiaomi::QrLogin::poll(transport, attempt), Xiaomi::MiFitnessAuthError);
}

TEST(XiaomiQrLogin, PollRejectsADeclinedOrMalformedAnswer) {
    FakeHttpTransport transport;
    const auto attempt = started(transport);
    transport.reply({200, kPrefix + R"({"code":70016})", {}});
    EXPECT_THROW(Xiaomi::QrLogin::poll(transport, attempt), Xiaomi::MiFitnessAuthError);
    transport.reply({200, kPrefix + R"({"code":0,"userId":1})", {}});
    EXPECT_THROW(Xiaomi::QrLogin::poll(transport, attempt), Xiaomi::MiFitnessAuthError);
    transport.reply({200, "not the expected shape", {}});
    EXPECT_THROW(Xiaomi::QrLogin::poll(transport, attempt), Xiaomi::MiFitnessAuthError);
}

TEST(XiaomiQrLogin, PollRefusesAStoredUrlOutsideXiaomi) {
    FakeHttpTransport transport;
    Xiaomi::QrLogin::Attempt attempt;
    attempt.poll_url = "https://evil.example/lp";
    EXPECT_THROW(Xiaomi::QrLogin::poll(transport, attempt), Xiaomi::MiFitnessAuthError);
    EXPECT_TRUE(transport.requests().empty());
}

TEST(XiaomiQrLogin, OtherNetworkFailuresOfThePollPropagate) {
    FakeHttpTransport transport;
    const auto attempt = started(transport);
    transport.reply_transport_error("Could not resolve host");
    EXPECT_THROW(Xiaomi::QrLogin::poll(transport, attempt), Xiaomi::MiFitnessProtocolError);
}
