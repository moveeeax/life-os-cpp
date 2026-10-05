/**
 * @file test_fitness_account_api.cpp
 * @brief The Mi account routes: QR linking over a scripted Xiaomi transport,
 *        link status, region, unlink, and who may see whose attempt.
 */

#include <cstddef>
#include <cstdint>
#include <string>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/FitnessController.hpp"
#include "cache/Cache.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "fitness/FakeHttpTransport.hpp"
#include "fitness/LinkService.hpp"
#include "fitness/xiaomi/RegionDetect.hpp"
#include "fitness/xiaomi/Service.hpp"
#include "repositories/fitness/MiAccountRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kTestKeyB64 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa1";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb1";
// The account id FakeHttpTransport::reply_login() answers with.
constexpr const char* kXiaomiId = "1234567890";
const std::string kPrefix = "&&&START&&&";
const std::string kFreshToken = "V1:fresh-token-from-the-qr-sign-in";

const char* const kEmpty = R"({"code":0,"result":{"data_list":[],"has_more":false}})";
const char* const kWithData = R"({"code":0,"result":{"data_list":[{"a":1}],"has_more":false}})";

Security::Auth::AuthPrincipal user(const std::string& id, std::uint32_t extra = Domain::Permission::kFitnessSync) {
    Security::Auth::AuthPrincipal p;
    p.subject = id;
    p.raw_claims =
        json{{"sub", id}, {"permissions", Domain::Permission::kGeneral | Domain::Permission::kFitnessRead | extra}};
    return p;
}

json body_of(const HttpResponsePtr& resp) {
    return json::parse(std::string(resp->body()));
}

class MiAccountApiTest : public TestHelpers::CoreBackedTest {
protected:
    FakeHttpTransport transport;
    Api::FitnessController controller;

    std::string config_file_name() const override { return "mi_account_api_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override {
        cfg["fitness"]["enabled"] = true;
        cfg["fitness"]["xiaomi"]["token_key"] = kTestKeyB64;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Xiaomi::Service::install_for_testing(&transport);
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE mi_accounts, sync_runs, heart_rate_samples");
            for (const char* id : {kAnna, kBoris}) {
                txn.exec_params(
                    "INSERT INTO users (id, email, confirmed, role_id) "
                    "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                    "ON CONFLICT DO NOTHING",
                    std::string(id),
                    std::string(id) + "@example.test");
            }
            return true;
        });
        // The start limit is counted in Redis and would carry over between tests.
        for (const char* id : {kAnna, kBoris}) {
            Cache::get().del(std::string("mi:link:rate:") + id);
        }
    }

    void TearDown() override {
        Xiaomi::Service::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    // ── scripted Xiaomi ──────────────────────────────────────────────────

    /// The three answers of a QR sign-in start.
    void reply_qr_start() {
        transport.reply({200,
                         kPrefix + R"({"code":70016,"callback":"https://sts-hlth.io.mi.com/healthapp/sts"})",
                         {{"Set-Cookie", "deviceId=dev1; Path=/"}}});
        transport.reply({200,
                         kPrefix +
                             R"({"code":0,"qr":"https://account.xiaomi.com/longPolling/qr?t=1",)"
                             R"("loginUrl":"https://account.xiaomi.com/longPolling/login?t=1&amp;sid=miothealth",)"
                             R"("lp":"https://account.xiaomi.com/longPolling/lp?t=1","timeout":300})",
                         {}});
        transport.reply({200, "PNGBYTES", {}});
    }

    /// The long poll answers: the user confirmed.
    void reply_confirmation(const std::string& xiaomi_id = kXiaomiId) {
        transport.reply({200,
                         kPrefix + R"({"code":0,"userId":)" + xiaomi_id + R"(,"passToken":")" + kFreshToken +
                             R"(","ssecurity":"c2VjcmV0","location":"https://sts-hlth.io.mi.com/healthapp/sts?d=1"})",
                         {}});
    }

    // ── routes ───────────────────────────────────────────────────────────

    HttpResponsePtr status(const Security::Auth::AuthPrincipal& p) {
        HttpResponsePtr captured;
        controller.accountStatus(TestHelpers::authed(p, Get), [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    HttpResponsePtr start(const Security::Auth::AuthPrincipal& p) {
        HttpResponsePtr captured;
        controller.accountLinkStart(TestHelpers::authed(p, Post), [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    HttpResponsePtr step(const Security::Auth::AuthPrincipal& p, const std::string& link_id) {
        HttpResponsePtr captured;
        controller.accountLinkStep(
            TestHelpers::authed(p, Get), [&](const HttpResponsePtr& r) { captured = r; }, link_id);
        return captured;
    }

    HttpResponsePtr unlink(const Security::Auth::AuthPrincipal& p, const json& body = json::object()) {
        HttpResponsePtr captured;
        controller.accountUnlink(TestHelpers::authed_json(p, body, Delete),
                                 [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    HttpResponsePtr patch(const Security::Auth::AuthPrincipal& p, const json& body) {
        HttpResponsePtr captured;
        controller.accountPatch(TestHelpers::authed_json(p, body, Patch),
                                [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    HttpResponsePtr detect(const Security::Auth::AuthPrincipal& p) {
        HttpResponsePtr captured;
        controller.accountDetectRegion(TestHelpers::authed(p, Post), [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    /// Start an attempt and return its id.
    std::string started_link(const Security::Auth::AuthPrincipal& p) {
        reply_qr_start();
        const auto resp = start(p);
        EXPECT_EQ(resp->statusCode(), k201Created) << resp->body();
        return body_of(resp)["data"]["link_id"].get<std::string>();
    }

    static void link_directly(const std::string& owner, const std::string& xiaomi_id, bool detected = true) {
        Repositories::MiAccountRepository(kTestKeyB64).link(owner, {xiaomi_id, std::string(347, 'S'), "cn"}, detected);
    }

    static long count(const std::string& sql) {
        return Database::get().execute_read([&](auto& txn) { return txn.exec(sql)[0][0].template as<long>(); });
    }
};

}  // namespace

TEST_F(MiAccountApiTest, LinksAnAccountFromStartToStatus) {
    const auto anna = user(kAnna);
    EXPECT_EQ(body_of(status(anna))["data"], json({{"status", "none"}}));

    reply_qr_start();
    const auto begun = start(anna);
    ASSERT_EQ(begun->statusCode(), k201Created) << begun->body();
    const json attempt = body_of(begun)["data"];
    const std::string link_id = attempt["link_id"].get<std::string>();
    EXPECT_EQ(link_id.size(), 32u);
    EXPECT_EQ(attempt["qr_png_base64"], "UE5HQllURVM=");  // base64 of PNGBYTES
    EXPECT_EQ(attempt["confirm_url"], "https://account.xiaomi.com/longPolling/login?t=1&sid=miothealth");
    EXPECT_EQ(attempt["expires_in_seconds"], 300);
    // The long-poll URL is what hands out the token: it stays on the server.
    EXPECT_EQ(std::string(begun->body()).find("longPolling/lp"), std::string::npos);

    // Not confirmed yet.
    transport.reply_timeout();
    const auto waiting = step(anna, link_id);
    ASSERT_EQ(waiting->statusCode(), k200OK);
    EXPECT_EQ(body_of(waiting)["data"]["state"], "pending");
    EXPECT_EQ(body_of(status(anna))["data"]["status"], "none");

    // Confirmed: login with the new token, then the region probe finds data in cn.
    reply_confirmation();
    transport.reply_login("ROTATED-BY-LOGIN");
    transport.reply_encrypted(kWithData);
    const auto done = step(anna, link_id);
    ASSERT_EQ(done->statusCode(), k200OK) << done->body();
    EXPECT_EQ(body_of(done)["data"]["state"], "linked");

    const auto linked = status(anna);
    const json s = body_of(linked)["data"];
    EXPECT_EQ(s["status"], "ok");
    EXPECT_EQ(s["account"], "******90");
    EXPECT_EQ(s["region"], "cn");
    EXPECT_EQ(s["region_detected"], true);
    EXPECT_FALSE(s["last_ok_at"].is_null());
    // Neither the token nor the full account id leaves the service.
    for (const auto& resp : {begun, done, linked}) {
        const std::string text(resp->body());
        EXPECT_EQ(text.find(kFreshToken), std::string::npos);
        EXPECT_EQ(text.find("ROTATED-BY-LOGIN"), std::string::npos);
        EXPECT_EQ(text.find(kXiaomiId), std::string::npos);
    }
    // What is stored is the token the login returned.
    const auto stored = Repositories::MiAccountRepository(kTestKeyB64).load(kAnna);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->user_id, kXiaomiId);
    EXPECT_EQ(stored->pass_token, "ROTATED-BY-LOGIN");

    // The attempt is over: its id answers 404 from now on.
    EXPECT_EQ(step(anna, link_id)->statusCode(), k404NotFound);
    // Boris still has nothing.
    EXPECT_EQ(body_of(status(user(kBoris)))["data"]["status"], "none");
}

TEST_F(MiAccountApiTest, AttemptBelongsToTheUserWhoStartedIt) {
    const std::string link_id = started_link(user(kAnna));
    const std::size_t requests_before = transport.requests().size();

    EXPECT_EQ(step(user(kBoris), link_id)->statusCode(), k404NotFound);
    EXPECT_EQ(step(user(kAnna), std::string(32, '0'))->statusCode(), k404NotFound);
    EXPECT_EQ(step(user(kAnna), "../../etc/passwd")->statusCode(), k404NotFound);
    EXPECT_EQ(step(user(kAnna), "")->statusCode(), k404NotFound);
    EXPECT_EQ(transport.requests().size(), requests_before) << "none of these may reach Xiaomi";

    // Anna's attempt is still alive after Boris tried it.
    transport.reply_timeout();
    EXPECT_EQ(body_of(step(user(kAnna), link_id))["data"]["state"], "pending");
}

TEST_F(MiAccountApiTest, AXiaomiAccountLinksToOneUserOnly) {
    link_directly(kAnna, kXiaomiId);
    const std::string link_id = started_link(user(kBoris));
    const std::size_t requests_before = transport.requests().size();

    reply_confirmation();
    const auto resp = step(user(kBoris), link_id);
    ASSERT_EQ(resp->statusCode(), k200OK);
    EXPECT_EQ(body_of(resp)["data"]["state"], "failed");
    EXPECT_EQ(body_of(resp)["data"]["error"], "account_linked_elsewhere");
    // Only the long poll went out: no login was made with the other user's account.
    EXPECT_EQ(transport.requests().size(), requests_before + 1);
    EXPECT_EQ(body_of(status(user(kBoris)))["data"]["status"], "none");
    EXPECT_EQ(step(user(kBoris), link_id)->statusCode(), k404NotFound);
}

TEST_F(MiAccountApiTest, AnotherXiaomiAccountNeedsAnUnlinkFirst) {
    link_directly(kAnna, "5555555555");
    const std::string link_id = started_link(user(kAnna));
    reply_confirmation();
    const auto resp = step(user(kAnna), link_id);
    EXPECT_EQ(body_of(resp)["data"]["state"], "failed");
    EXPECT_EQ(body_of(resp)["data"]["error"], "different_account");
    EXPECT_EQ(Repositories::MiAccountRepository(kTestKeyB64).load(kAnna)->user_id, "5555555555");
}

TEST_F(MiAccountApiTest, LinkingAgainReplacesTheTokenAfterReauth) {
    link_directly(kAnna, kXiaomiId);
    Repositories::MiAccountRepository(kTestKeyB64).mark_reauth_required(kAnna, "upstream_auth");
    EXPECT_EQ(body_of(status(user(kAnna)))["data"]["status"], "reauth_required");
    EXPECT_EQ(body_of(status(user(kAnna)))["data"]["last_error"], "upstream_auth");

    const std::string link_id = started_link(user(kAnna));
    reply_confirmation();
    transport.reply_login("TOKEN-AFTER-RELINK");
    transport.reply_encrypted(kWithData);
    EXPECT_EQ(body_of(step(user(kAnna), link_id))["data"]["state"], "linked");

    EXPECT_EQ(body_of(status(user(kAnna)))["data"]["status"], "ok");
    EXPECT_EQ(Repositories::MiAccountRepository(kTestKeyB64).load(kAnna)->pass_token, "TOKEN-AFTER-RELINK");
    EXPECT_EQ(count("SELECT COUNT(*) FROM mi_accounts"), 1);
}

TEST_F(MiAccountApiTest, ARefusedLoginWithTheNewTokenStoresNothing) {
    const std::string link_id = started_link(user(kAnna));
    reply_confirmation();
    transport.reply({200, "no start prefix at all", {}});  // the login is refused

    const auto resp = step(user(kAnna), link_id);
    ASSERT_EQ(resp->statusCode(), k200OK);
    EXPECT_EQ(body_of(resp)["data"]["state"], "failed");
    EXPECT_EQ(body_of(resp)["data"]["error"], "xiaomi_refused");
    EXPECT_EQ(body_of(status(user(kAnna)))["data"]["status"], "none");
}

TEST_F(MiAccountApiTest, AnAnswerPointingOutsideXiaomiEndsTheAttempt) {
    const std::string link_id = started_link(user(kAnna));
    const std::size_t requests_before = transport.requests().size();
    transport.reply({200,
                     kPrefix + R"({"code":0,"userId":1234567890,"passToken":"t","ssecurity":"c2VjcmV0",)"
                               R"("location":"https://evil.example/sts"})",
                     {}});

    const auto resp = step(user(kAnna), link_id);
    EXPECT_EQ(body_of(resp)["data"]["state"], "failed");
    EXPECT_EQ(body_of(resp)["data"]["error"], "xiaomi_refused");
    EXPECT_EQ(transport.requests().size(), requests_before + 1) << "nothing may follow the bad answer";
    EXPECT_EQ(step(user(kAnna), link_id)->statusCode(), k404NotFound);
}

TEST_F(MiAccountApiTest, ANetworkHiccupKeepsTheAttemptAlive) {
    const std::string link_id = started_link(user(kAnna));
    transport.reply_transport_error("Could not resolve host");
    EXPECT_EQ(body_of(step(user(kAnna), link_id))["data"]["state"], "pending");
    transport.reply_timeout();
    EXPECT_EQ(body_of(step(user(kAnna), link_id))["data"]["state"], "pending");
}

TEST_F(MiAccountApiTest, AnAccountWithoutDataLinksWithARegionToChoose) {
    const std::string link_id = started_link(user(kAnna));
    reply_confirmation();
    transport.reply_login();
    for (std::size_t i = 0; i < Xiaomi::kRegionProbeOrder.size(); ++i) {
        transport.reply_encrypted(kEmpty);
    }
    EXPECT_EQ(body_of(step(user(kAnna), link_id))["data"]["state"], "linked");
    json s = body_of(status(user(kAnna)))["data"];
    EXPECT_EQ(s["region"], "cn");
    EXPECT_EQ(s["region_detected"], false);

    // The user picks the region.
    EXPECT_EQ(patch(user(kAnna), json{{"region", "moon"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(patch(user(kAnna), json{{"region", ""}})->statusCode(), k400BadRequest);
    EXPECT_EQ(patch(user(kAnna), json::object())->statusCode(), k400BadRequest);
    const auto patched = patch(user(kAnna), json{{"region", "de"}});
    ASSERT_EQ(patched->statusCode(), k200OK);
    EXPECT_EQ(body_of(patched)["data"]["region"], "de");
    EXPECT_EQ(body_of(patched)["data"]["region_detected"], true);
    // Without a link there is nothing to patch.
    EXPECT_EQ(patch(user(kBoris), json{{"region", "de"}})->statusCode(), k404NotFound);
}

TEST_F(MiAccountApiTest, DetectRegionFindsTheRegionWithData) {
    link_directly(kAnna, kXiaomiId, /*detected=*/false);
    transport.reply_login();
    transport.reply_encrypted(kEmpty);     // cn
    transport.reply_encrypted(kWithData);  // sg
    const auto resp = detect(user(kAnna));
    ASSERT_EQ(resp->statusCode(), k200OK) << resp->body();
    EXPECT_EQ(body_of(resp)["data"]["region"], "sg");
    EXPECT_EQ(body_of(resp)["data"]["region_detected"], true);

    EXPECT_EQ(detect(user(kBoris))->statusCode(), k404NotFound);
}

TEST_F(MiAccountApiTest, DetectRegionWithARefusedTokenAsksToLinkAgain) {
    link_directly(kAnna, kXiaomiId);
    transport.reply({200, "no start prefix at all", {}});
    const auto resp = detect(user(kAnna));
    EXPECT_EQ(resp->statusCode(), k503ServiceUnavailable);
    EXPECT_EQ(body_of(resp)["error"], "upstream_auth");
    EXPECT_EQ(body_of(status(user(kAnna)))["data"]["status"], "reauth_required");
}

TEST_F(MiAccountApiTest, SixthStartInTheWindowIsRateLimited) {
    for (int i = 0; i < Fitness::Link::kMaxStartsPerWindow; ++i) {
        reply_qr_start();
        ASSERT_EQ(start(user(kAnna))->statusCode(), k201Created) << "start " << i;
    }
    const std::size_t requests_before = transport.requests().size();
    EXPECT_EQ(start(user(kAnna))->statusCode(), k429TooManyRequests);
    EXPECT_EQ(transport.requests().size(), requests_before) << "a limited start must not reach Xiaomi";
    // The limit is per user.
    reply_qr_start();
    EXPECT_EQ(start(user(kBoris))->statusCode(), k201Created);
}

TEST_F(MiAccountApiTest, XiaomiNotIssuingASignInIs503) {
    transport.reply({503, "", {}});
    const auto resp = start(user(kAnna));
    EXPECT_EQ(resp->statusCode(), k503ServiceUnavailable);
    EXPECT_EQ(body_of(resp)["error"], "upstream_unavailable");
}

TEST_F(MiAccountApiTest, UnlinkKeepsOrDeletesTheData) {
    EXPECT_EQ(unlink(user(kAnna))->statusCode(), k404NotFound);

    link_directly(kAnna, kXiaomiId);
    link_directly(kBoris, "5555555555");
    Database::get().execute_write([](auto& txn) {
        txn.exec(
            "INSERT INTO heart_rate_samples (user_id, timestamp, bpm, sample_type) VALUES "
            "('1234567890', now(), 60, 'passive'), ('5555555555', now(), 70, 'passive')");
        return true;
    });

    EXPECT_EQ(unlink(user(kAnna), json{{"delete_data", "yes"}})->statusCode(), k400BadRequest);

    // A running sync blocks the unlink.
    Database::get().execute_write([](auto& txn) {
        txn.exec("INSERT INTO sync_runs (status) VALUES ('running')");
        return true;
    });
    EXPECT_EQ(unlink(user(kAnna))->statusCode(), k409Conflict);
    Database::get().execute_write([](auto& txn) {
        txn.exec("TRUNCATE TABLE sync_runs");
        return true;
    });

    ASSERT_EQ(unlink(user(kAnna))->statusCode(), k200OK);
    EXPECT_EQ(body_of(status(user(kAnna)))["data"]["status"], "none");
    EXPECT_EQ(count("SELECT COUNT(*) FROM heart_rate_samples"), 2) << "data stays without delete_data";

    link_directly(kAnna, kXiaomiId);
    ASSERT_EQ(unlink(user(kAnna), json{{"delete_data", true}})->statusCode(), k200OK);
    EXPECT_EQ(count("SELECT COUNT(*) FROM heart_rate_samples WHERE user_id = '1234567890'"), 0);
    EXPECT_EQ(count("SELECT COUNT(*) FROM heart_rate_samples WHERE user_id = '5555555555'"), 1);
    EXPECT_EQ(body_of(status(user(kBoris)))["data"]["status"], "ok");
}

TEST_F(MiAccountApiTest, RoutesNeedFitnessSyncAndAUserAccount) {
    const auto reader = user(kAnna, /*extra=*/0);
    EXPECT_EQ(status(reader)->statusCode(), k403Forbidden);
    EXPECT_EQ(start(reader)->statusCode(), k403Forbidden);
    EXPECT_EQ(step(reader, std::string(32, 'a'))->statusCode(), k403Forbidden);
    EXPECT_EQ(unlink(reader)->statusCode(), k403Forbidden);
    EXPECT_EQ(patch(reader, json{{"region", "cn"}})->statusCode(), k403Forbidden);
    EXPECT_EQ(detect(reader)->statusCode(), k403Forbidden);

    // A static bearer is not a user: there is nobody to link an account to.
    const auto bearer = user("static-bearer");
    const auto resp = status(bearer);
    EXPECT_EQ(resp->statusCode(), k403Forbidden);
    EXPECT_EQ(body_of(resp)["error"], "no_user_account");
    EXPECT_EQ(start(bearer)->statusCode(), k403Forbidden);

    HttpResponsePtr anonymous;
    controller.accountStatus(TestHelpers::make_request(Get), [&](const HttpResponsePtr& r) { anonymous = r; });
    ASSERT_TRUE(anonymous);
    EXPECT_NE(anonymous->statusCode(), k200OK);
    EXPECT_TRUE(transport.requests().empty());
}

namespace {

class MiAccountNoKeyTest : public TestHelpers::CoreBackedTest {
protected:
    Api::FitnessController controller;

    std::string config_file_name() const override { return "mi_account_no_key_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override { cfg["fitness"]["enabled"] = true; }
};

class MiAccountDisabledTest : public TestHelpers::CoreBackedTest {
protected:
    Api::FitnessController controller;

    std::string config_file_name() const override { return "mi_account_disabled_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override { cfg["fitness"]["enabled"] = false; }
};

}  // namespace

TEST_F(MiAccountNoKeyTest, WithoutATokenKeyNothingCanBeLinked) {
    HttpResponsePtr started;
    controller.accountLinkStart(TestHelpers::authed(user(kAnna), Post), [&](const HttpResponsePtr& r) { started = r; });
    ASSERT_TRUE(started);
    EXPECT_EQ(started->statusCode(), k503ServiceUnavailable);
    EXPECT_EQ(body_of(started)["error"], "not_configured");
}

TEST_F(MiAccountDisabledTest, RoutesAre404WhileTheModuleIsOff) {
    HttpResponsePtr resp;
    controller.accountStatus(TestHelpers::authed(user(kAnna), Get), [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_TRUE(resp);
    EXPECT_EQ(resp->statusCode(), k404NotFound);

    resp.reset();
    controller.accountLinkStart(TestHelpers::authed(user(kAnna), Post), [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_TRUE(resp);
    EXPECT_EQ(resp->statusCode(), k404NotFound);
}
