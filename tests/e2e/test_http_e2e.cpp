/**
 * @file test_http_e2e.cpp
 * @brief End-to-end tests over REAL HTTP.
 *
 * Every other suite drives controller methods directly, which means the
 * middleware chain registered in Api::register_controllers() — auth gate,
 * content-type check, idempotency, tracing headers, Drogon routing, cookie
 * serialization on the wire — never executes in tests. This binary closes
 * that gap: it boots Core, registers the controllers, runs the real Drogon
 * server on a loopback port in a background thread, and talks to it with
 * drogon::HttpClient.
 *
 * Built as a SEPARATE executable (life_os_cpp_e2e): drogon::app() is a
 * process-wide singleton whose run()/quit() cycle is once-per-process, so it
 * cannot coexist with suites that reset global state between tests.
 *
 * Requires the Postgres + Redis sidecars (same as the integration bucket).
 * Run inside Docker:  make test-e2e
 */

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <drogon/HttpClient.h>
#include <drogon/drogon.h>
#include <gtest/gtest.h>
#include <trantor/net/EventLoopThread.h>

#include <nlohmann/json.hpp>

#include "api/Api.hpp"
#include "core/Core.hpp"
#include "domain/Role.hpp"
#include "openapi_check.hpp"
#include "security/Auth.hpp"
#include "security/Jwt.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kSecret = "e2e-test-jwt-secret-0123456789-abcdef";  // >=32 chars (Auth boot guard)
constexpr uint16_t kPort = 18098;

bool g_env_ok = false;

std::string base_url() {
    return "http://127.0.0.1:" + std::to_string(kPort);
}

/**
 * Dedicated event loop for every HttpClient in this binary.
 *
 * newHttpClient(url) with no explicit loop falls back to app().getLoop() — a
 * lazily constructed static EventLoop that binds to whichever thread touches
 * it first. That races the server thread's app().run(): if the main thread
 * (client) constructs it first, run() aborts with trantor's "It is forbidden
 * to run loop on threads other than event-loop thread" FATAL. Keeping clients
 * on a private loop means the main thread never touches the app loop at all.
 */
trantor::EventLoop* client_loop() {
    static trantor::EventLoopThread th("e2eClient");
    static const bool started = (th.run(), true);
    (void)started;
    return th.getLoop();
}

/**
 * Boots Core + Drogon exactly once for the whole binary. Tests skip when
 * the sidecars are unreachable (g_env_ok stays false).
 */
class HttpServerEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        std::filesystem::create_directories("logs");
        if (!TestHelpers::is_postgres_available() || !TestHelpers::is_redis_available()) {
            return;  // tests skip, or fail under CI_REQUIRE_INFRA
        }

        json cfg = json::parse(TestHelpers::minimal_config());
        cfg["auth"]["mode"] = "jwt";
        cfg["auth"]["jwt"]["secret"] = kSecret;
        cfg["auth"]["cookies"]["enabled"] = true;
        cfg["auth"]["cookies"]["secure"] = false;  // plain http in the test net
        cfg["idempotency"]["enabled"] = true;
        cfg["mail"]["enabled"] = false;
        cfg["database"]["migrations_enabled"] = true;
        cfg["database"]["migrations_dir"] = "migrations";

        config_path_ = TestHelpers::create_temp_config(cfg.dump(2), "e2e_test_config.json");
        Core::initialize(config_path_);

        TestHelpers::truncate_users();

        Api::register_controllers();
        app().addListener("127.0.0.1", kPort).setThreadNum(1);
        server_thread_ = std::thread([] { app().run(); });

        // Wait until the server actually accepts: poll /healthz.
        auto client = HttpClient::newHttpClient(base_url(), client_loop());
        for (int i = 0; i < 100; ++i) {
            auto req = HttpRequest::newHttpRequest();
            req->setPath("/healthz");
            auto [ok, resp] = client->sendRequest(req, /*timeout=*/1.0);
            if (ok == ReqResult::Ok && resp && resp->statusCode() == k200OK) {
                g_env_ok = true;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ADD_FAILURE() << "e2e server did not become ready on " << base_url();
    }

    void TearDown() override {
        if (server_thread_.joinable()) {
            app().quit();
            server_thread_.join();
        }
        TestHelpers::reset_all_globals();
        TestHelpers::remove_temp_config(config_path_);
    }

private:
    std::string config_path_;
    std::thread server_thread_;
};

// Skips locally, fails under CI_REQUIRE_INFRA (TestHelpers::skip_or_fail_without_infra).
#define REQUIRE_E2E_ENV() \
    if (!g_env_ok)        \
    return TestHelpers::skip_or_fail_without_infra("the e2e server (Postgres/Redis sidecars)")

// ---------------------------------------------------------------------------
// Small sync-request helpers.
// ---------------------------------------------------------------------------

HttpResponsePtr send(const HttpRequestPtr& req) {
    auto client = HttpClient::newHttpClient(base_url(), client_loop());
    auto [ok, resp] = client->sendRequest(req, /*timeout=*/5.0);
    EXPECT_EQ(ok, ReqResult::Ok) << "transport error talking to e2e server";
    EXPECT_NE(resp, nullptr);
    return resp;
}

HttpRequestPtr json_post(const std::string& path, const json& body) {
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->setPath(path);
    req->setBody(body.dump());
    req->setContentTypeCode(CT_APPLICATION_JSON);
    return req;
}

json body_of(const HttpResponsePtr& resp) {
    return json::parse(std::string(resp->getBody()));
}

/**
 * Validate a real response body against the schema docs/openapi.yaml declares
 * for (method, spec path template, ACTUAL status). Thin adapter over
 * OpenApiCheck::expect_matches_schema — see tests/e2e/openapi_check.hpp for
 * the supported schema subset and the explicit-SKIP discipline.
 */
void expect_matches_schema(const HttpResponsePtr& resp, const std::string& method, const std::string& spec_path) {
    ASSERT_NE(resp, nullptr);
    OpenApiCheck::expect_matches_schema(
        std::string(resp->getBody()), method, spec_path, static_cast<int>(resp->statusCode()));
}

struct SessionCookies {
    std::string access;
    std::string refresh;
};

SessionCookies cookies_of(const HttpResponsePtr& resp) {
    SessionCookies out;
    for (const auto& [name, c] : resp->getCookies()) {
        if (name.find("access") != std::string::npos)
            out.access = c.value();
        else if (name.find("refresh") != std::string::npos)
            out.refresh = c.value();
    }
    return out;
}

void attach_session(const HttpRequestPtr& req, const SessionCookies& sc) {
    const auto& cookie_cfg = Security::Auth::get().config().cookies;
    if (!sc.access.empty())
        req->addCookie(cookie_cfg.access_name, sc.access);
    if (!sc.refresh.empty())
        req->addCookie(cookie_cfg.refresh_name, sc.refresh);
}

SessionCookies register_and_login(const std::string& email, const std::string& password) {
    auto reg = send(json_post("/api/v1/auth/register", {{"email", email}, {"password", password}}));
    EXPECT_EQ(reg->statusCode(), k201Created) << reg->getBody();
    expect_matches_schema(reg, "POST", "/api/v1/auth/register");
    auto login = send(json_post("/api/v1/auth/login", {{"email", email}, {"password", password}}));
    EXPECT_EQ(login->statusCode(), k200OK) << login->getBody();
    expect_matches_schema(login, "POST", "/api/v1/auth/login");
    return cookies_of(login);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Deliberately NOT gated on REQUIRE_E2E_ENV: this is the drift gate between
// docs/openapi.yaml and the committed tests/e2e/openapi.gen.json the schema
// checks below consume — it must fail even on a machine without the sidecars.
TEST(OpenApiSpec, GenJsonIsFreshAndLoadable) {
    OpenApiCheck::expect_spec_json_fresh();
}

TEST(HttpE2E, HealthzCarriesRequestIdHeader) {
    REQUIRE_E2E_ENV();
    auto req = HttpRequest::newHttpRequest();
    req->setPath("/healthz");
    auto resp = send(req);
    EXPECT_EQ(resp->statusCode(), k200OK);
    EXPECT_EQ(body_of(resp)["status"], "alive");
    expect_matches_schema(resp, "GET", "/healthz");
    // Tracing middleware must stamp every response.
    EXPECT_FALSE(resp->getHeader("x-request-id").empty());
}

TEST(HttpE2E, SecurityHeadersStampedOnEveryResponse) {
    REQUIRE_E2E_ENV();
    auto req = HttpRequest::newHttpRequest();
    req->setPath("/healthz");
    auto resp = send(req);
    EXPECT_EQ(resp->getHeader("x-content-type-options"), "nosniff");
    EXPECT_EQ(resp->getHeader("x-frame-options"), "DENY");
    EXPECT_EQ(resp->getHeader("referrer-policy"), "no-referrer");
    // API responses are JSON — CSP is locked down (default-src 'none').
    EXPECT_FALSE(resp->getHeader("content-security-policy").empty());
}

TEST(HttpE2E, TraceparentPropagatesToResponse) {
    REQUIRE_E2E_ENV();
    auto req = HttpRequest::newHttpRequest();
    req->setPath("/healthz");
    req->addHeader("traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01");
    auto resp = send(req);
    EXPECT_EQ(resp->getHeader("x-request-id"), "0af7651916cd43dd8448eb211c80319c");
}

TEST(HttpE2E, NonJsonContentTypeRejectedWith415) {
    REQUIRE_E2E_ENV();
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->setPath("/api/v1/auth/login");
    req->setBody("email=not-json");
    req->setContentTypeCode(CT_TEXT_PLAIN);
    auto resp = send(req);
    EXPECT_EQ(resp->statusCode(), k415UnsupportedMediaType);
    expect_matches_schema(resp, "POST", "/api/v1/auth/login");
    // Short-circuited responses (sync advices skip the whole post-handling
    // chain) must still carry the observability + security headers —
    // middleware::short_circuit replays them. Before it existed, a 415/401/429
    // had no request id, no headers, no log line, no metric sample.
    EXPECT_FALSE(resp->getHeader("x-request-id").empty());
    EXPECT_EQ(resp->getHeader("x-content-type-options"), "nosniff");
}

TEST(HttpE2E, ContentTypeComparisonIsCaseInsensitive) {
    // RFC 9110 §8.3.1: media types/subtypes are case-insensitive, so
    // `Application/JSON` is legal and must NOT 415. (It reaches the login
    // handler, which answers 400/401 for the junk credentials.)
    REQUIRE_E2E_ENV();
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->setPath("/api/v1/auth/login");
    req->setBody(R"({"email":"nobody@example.com","password":"wrong-password"})");
    req->setContentTypeString("Application/JSON; charset=UTF-8");
    auto resp = send(req);
    EXPECT_NE(resp->statusCode(), k415UnsupportedMediaType);
    // Junk-credential path — validates the 401 error body against the spec.
    expect_matches_schema(resp, "POST", "/api/v1/auth/login");
}

TEST(HttpE2E, AuthMiddlewareGuardsNonPublicPaths) {
    REQUIRE_E2E_ENV();
    auto req = HttpRequest::newHttpRequest();
    req->setPath("/api/v1/jobs");  // not in api.public_paths
    auto resp = send(req);
    EXPECT_EQ(resp->statusCode(), k401Unauthorized);
    expect_matches_schema(resp, "GET", "/api/v1/jobs");
    EXPECT_FALSE(resp->getHeader("www-authenticate").empty());
    // The auth advice short-circuits before any pre/post advice — the reply
    // must still be observable and hardened (see middleware::short_circuit).
    EXPECT_FALSE(resp->getHeader("x-request-id").empty());
    EXPECT_EQ(resp->getHeader("x-content-type-options"), "nosniff");
}

TEST(HttpE2E, MalformedBearerTokenIs401NotAnException) {
    // The header segment is read before the signature check. "MQ.MQ.MQ"
    // decodes to the JSON scalar 1 in every segment; a token like this used
    // to throw out of the auth advice instead of answering 401.
    REQUIRE_E2E_ENV();
    for (const char* token : {"MQ.MQ.MQ", "eyJhbGciOjF9.MQ.MQ"}) {  // {"alg":1}
        auto req = HttpRequest::newHttpRequest();
        req->setPath("/api/v1/jobs");
        req->addHeader("Authorization", std::string("Bearer ") + token);
        auto resp = send(req);
        EXPECT_EQ(resp->statusCode(), k401Unauthorized) << token << " " << resp->getBody();
        expect_matches_schema(resp, "GET", "/api/v1/jobs");
        EXPECT_FALSE(resp->getHeader("x-request-id").empty());
    }
}

TEST(HttpE2E, AccountTokenRoutesArePublic) {
    // The token-bearing account routes must reach their handler WITHOUT a
    // session (the user clicking an email link isn't logged in). Pre-fix the
    // wildcard public path was missing and the auth middleware 401'd here,
    // breaking confirm/reset/change-email end-to-end. We expect the handler's
    // own 400 invalid_token (bad token), NOT a 401 from the middleware.
    REQUIRE_E2E_ENV();
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->setPath("/api/v1/account/reset-password/not-a-real-token");
    req->setBody(R"({"new_password":"whatever-123"})");
    req->setContentTypeCode(CT_APPLICATION_JSON);  // pass the content-type gate
    auto resp = send(req);
    EXPECT_EQ(resp->statusCode(), k400BadRequest) << resp->getBody();
    EXPECT_EQ(body_of(resp)["error"], "invalid_token");
    expect_matches_schema(resp, "POST", "/api/v1/account/reset-password/{token}");
}

TEST(HttpE2E, RegisterLoginMeRoundtripOverWire) {
    REQUIRE_E2E_ENV();
    auto sc = register_and_login("e2e-alice@example.com", "password-e2e-1");
    ASSERT_FALSE(sc.access.empty()) << "no access cookie on the wire";
    ASSERT_FALSE(sc.refresh.empty()) << "no refresh cookie on the wire";

    auto me = HttpRequest::newHttpRequest();
    me->setPath("/api/v1/auth/me");
    attach_session(me, sc);
    auto resp = send(me);
    ASSERT_EQ(resp->statusCode(), k200OK) << resp->getBody();
    EXPECT_EQ(body_of(resp)["user"]["email"], "e2e-alice@example.com");
    expect_matches_schema(resp, "GET", "/api/v1/auth/me");
}

TEST(HttpE2E, RefreshRotatesSession) {
    REQUIRE_E2E_ENV();
    auto sc = register_and_login("e2e-bob@example.com", "password-e2e-1");

    auto refresh = HttpRequest::newHttpRequest();
    refresh->setMethod(Post);
    refresh->setPath("/api/v1/auth/refresh");
    attach_session(refresh, sc);
    auto resp = send(refresh);
    ASSERT_EQ(resp->statusCode(), k200OK) << resp->getBody();
    expect_matches_schema(resp, "POST", "/api/v1/auth/refresh");

    auto rotated = cookies_of(resp);
    EXPECT_FALSE(rotated.refresh.empty());
    EXPECT_NE(rotated.refresh, sc.refresh) << "refresh token must rotate";
}

TEST(HttpE2E, LogoutRevokesRefreshToken) {
    REQUIRE_E2E_ENV();
    auto sc = register_and_login("e2e-carol@example.com", "password-e2e-1");

    auto logout = HttpRequest::newHttpRequest();
    logout->setMethod(Post);
    logout->setPath("/api/v1/auth/logout");
    attach_session(logout, sc);
    auto logout_resp = send(logout);
    ASSERT_EQ(logout_resp->statusCode(), k200OK);
    expect_matches_schema(logout_resp, "POST", "/api/v1/auth/logout");

    // The old refresh JTI is revoked in Redis — rotation must now fail.
    auto refresh = HttpRequest::newHttpRequest();
    refresh->setMethod(Post);
    refresh->setPath("/api/v1/auth/refresh");
    attach_session(refresh, sc);
    auto denied = send(refresh);
    EXPECT_EQ(denied->statusCode(), k401Unauthorized);
    expect_matches_schema(denied, "POST", "/api/v1/auth/refresh");
}

TEST(HttpE2E, LogoutWorksAfterTheAccessCookieExpired) {
    // The browser drops an expired access cookie, so a late sign-out carries
    // only the refresh cookie. Logout must still reach its handler (it is
    // auth-public) and revoke the refresh JTI; behind the auth gate it got a
    // 401 and the refresh token stayed usable for its whole TTL.
    REQUIRE_E2E_ENV();
    auto sc = register_and_login("e2e-late-logout@example.com", "password-e2e-1");
    SessionCookies refresh_only;
    refresh_only.refresh = sc.refresh;

    auto logout = HttpRequest::newHttpRequest();
    logout->setMethod(Post);
    logout->setPath("/api/v1/auth/logout");
    attach_session(logout, refresh_only);
    auto logout_resp = send(logout);
    ASSERT_EQ(logout_resp->statusCode(), k200OK) << logout_resp->getBody();
    expect_matches_schema(logout_resp, "POST", "/api/v1/auth/logout");

    auto refresh = HttpRequest::newHttpRequest();
    refresh->setMethod(Post);
    refresh->setPath("/api/v1/auth/refresh");
    attach_session(refresh, refresh_only);
    auto denied = send(refresh);
    EXPECT_EQ(denied->statusCode(), k401Unauthorized);
}

TEST(HttpE2E, LogoutWithoutAnySessionStillSucceeds) {
    REQUIRE_E2E_ENV();
    auto logout = HttpRequest::newHttpRequest();
    logout->setMethod(Post);
    logout->setPath("/api/v1/auth/logout");
    auto resp = send(logout);
    EXPECT_EQ(resp->statusCode(), k200OK) << resp->getBody();
}

TEST(HttpE2E, RegistrationProbeIsPublic) {
    REQUIRE_E2E_ENV();
    auto req = HttpRequest::newHttpRequest();
    req->setPath("/api/v1/auth/registration");
    auto resp = send(req);
    ASSERT_EQ(resp->statusCode(), k200OK) << resp->getBody();
    EXPECT_EQ(body_of(resp)["data"]["open"], true);
    expect_matches_schema(resp, "GET", "/api/v1/auth/registration");
}

TEST(HttpE2E, ApiKeyIsRefusedOnTheAccountSurface) {
    // A key may read the owner's data but never manage the account: with a
    // leaked key one could otherwise mint more keys.
    REQUIRE_E2E_ENV();
    auto sc = register_and_login("e2e-keys@example.com", "password-e2e-1");
    auto create = json_post("/api/v1/account/api-keys", {{"name", "e2e"}});
    attach_session(create, sc);
    auto created = send(create);
    ASSERT_EQ(created->statusCode(), k201Created) << created->getBody();
    const std::string key = body_of(created)["key"];

    auto list = HttpRequest::newHttpRequest();
    list->setPath("/api/v1/account/api-keys");
    list->addHeader("X-API-Key", key);
    auto refused = send(list);
    EXPECT_EQ(refused->statusCode(), k401Unauthorized) << refused->getBody();
    EXPECT_EQ(body_of(refused)["error"], "key_not_allowed");

    auto mint = json_post("/api/v1/account/api-keys", {{"name", "second"}});
    mint->addHeader("X-API-Key", key);
    auto mint_resp = send(mint);
    EXPECT_EQ(mint_resp->statusCode(), k401Unauthorized) << mint_resp->getBody();

    auto admin = HttpRequest::newHttpRequest();
    admin->setPath("/api/v1/admin/users");
    admin->addHeader("X-API-Key", key);
    EXPECT_EQ(send(admin)->statusCode(), k401Unauthorized);

    // Other spellings of the refused paths are refused too (the middleware
    // canonicalizes before deciding).
    for (const char* path : {"/API/v1/admin/users",
                             "//api/v1/admin/users",
                             "/api/v1/./admin/users",
                             "/api/v1/x/../admin/users",
                             "/api/v1/Account/api-keys"}) {
        auto odd = HttpRequest::newHttpRequest();
        odd->setPath(path);
        odd->addHeader("X-API-Key", key);
        auto odd_resp = send(odd);
        EXPECT_EQ(odd_resp->statusCode(), k401Unauthorized) << path << " " << odd_resp->getBody();
        EXPECT_EQ(body_of(odd_resp)["error"], "key_not_allowed") << path;
    }

    // The same key still works where keys belong.
    auto me = HttpRequest::newHttpRequest();
    me->setPath("/api/v1/auth/me");
    me->addHeader("X-API-Key", key);
    auto me_resp = send(me);
    EXPECT_EQ(me_resp->statusCode(), k200OK) << me_resp->getBody();
}

TEST(HttpE2E, ReadKeyReadsButNeverWrites) {
    // A key of scope read answers GET and refuses every writing method with
    // 403 key_read_only, decided by the middleware before any handler.
    REQUIRE_E2E_ENV();
    auto sc = register_and_login("e2e-readkey@example.com", "password-e2e-1");
    auto create = json_post("/api/v1/account/api-keys", {{"name", "reader"}, {"scope", "read"}});
    attach_session(create, sc);
    auto created = send(create);
    ASSERT_EQ(created->statusCode(), k201Created) << created->getBody();
    EXPECT_EQ(body_of(created)["scope"], "read");
    const std::string key = body_of(created)["key"];

    auto me = HttpRequest::newHttpRequest();
    me->setPath("/api/v1/auth/me");
    me->addHeader("X-API-Key", key);
    auto me_resp = send(me);
    EXPECT_EQ(me_resp->statusCode(), k200OK) << me_resp->getBody();

    auto write = json_post("/api/v1/tasks/items", {{"title", "never"}});
    write->addHeader("X-API-Key", key);
    auto refused = send(write);
    EXPECT_EQ(refused->statusCode(), k403Forbidden) << refused->getBody();
    EXPECT_EQ(body_of(refused)["error"], "key_read_only");

    auto head = HttpRequest::newHttpRequest();
    head->setMethod(Head);
    head->setPath("/api/v1/auth/me");
    head->addHeader("X-API-Key", key);
    EXPECT_NE(send(head)->statusCode(), k403Forbidden);
}

TEST(HttpE2E, IdempotencyKeyReplaysResponse) {
    REQUIRE_E2E_ENV();
    const json body = {{"email", "e2e-idem@example.com"}, {"password", "password-e2e-1"}};

    auto first = json_post("/api/v1/auth/register", body);
    first->addHeader("Idempotency-Key", "e2e-key-001");
    auto r1 = send(first);
    ASSERT_EQ(r1->statusCode(), k201Created) << r1->getBody();
    expect_matches_schema(r1, "POST", "/api/v1/auth/register");

    // Identical retry: without the middleware this would be 409 email_taken;
    // with it, the cached 201 is replayed.
    auto second = json_post("/api/v1/auth/register", body);
    second->addHeader("Idempotency-Key", "e2e-key-001");
    auto r2 = send(second);
    EXPECT_EQ(r2->statusCode(), k201Created) << r2->getBody();
    EXPECT_EQ(r2->getHeader("x-idempotent-replayed"), "true");
    // The replayed body must STILL match the spec — a cached response is a
    // response.
    expect_matches_schema(r2, "POST", "/api/v1/auth/register");

    // Same key + DIFFERENT body → 422 conflict.
    auto third =
        json_post("/api/v1/auth/register", {{"email", "e2e-other@example.com"}, {"password", "password-e2e-1"}});
    third->addHeader("Idempotency-Key", "e2e-key-001");
    auto r3 = send(third);
    EXPECT_EQ(r3->statusCode(), k422UnprocessableEntity);
    expect_matches_schema(r3, "POST", "/api/v1/auth/register");
}

TEST(HttpE2E, AdminGateChecksPermissionBitmask) {
    REQUIRE_E2E_ENV();
    const auto now = Utils::Time::now_epoch_seconds();

    json admin_claims = {
        {"sub", "e2e-admin"}, {"iat", now}, {"exp", now + 600}, {"permissions", Domain::Permission::kAdminister}};
    json user_claims = {{"sub", "e2e-user"}, {"iat", now}, {"exp", now + 600}, {"permissions", 1}};
    const auto admin_jwt = Security::Auth::issue_hs256_jwt(admin_claims, kSecret);
    const auto user_jwt = Security::Auth::issue_hs256_jwt(user_claims, kSecret);

    auto as_admin = HttpRequest::newHttpRequest();
    as_admin->setPath("/api/v1/admin/users");
    as_admin->addHeader("Authorization", "Bearer " + admin_jwt);
    auto admin_resp = send(as_admin);
    EXPECT_EQ(admin_resp->statusCode(), k200OK);
    expect_matches_schema(admin_resp, "GET", "/api/v1/admin/users");

    auto as_user = HttpRequest::newHttpRequest();
    as_user->setPath("/api/v1/admin/users");
    as_user->addHeader("Authorization", "Bearer " + user_jwt);
    auto user_resp = send(as_user);
    EXPECT_EQ(user_resp->statusCode(), k403Forbidden);
    expect_matches_schema(user_resp, "GET", "/api/v1/admin/users");
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new HttpServerEnvironment);
    return RUN_ALL_TESTS();
}
