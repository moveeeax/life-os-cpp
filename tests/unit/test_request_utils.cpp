/**
 * @file test_request_utils.cpp
 * @brief Unit tests for the path-normalizer / pagination helpers in
 *        src/api/RequestUtils.hpp (pure, no services).
 *
 * Path normalization does double duty: cardinality control for metrics/traces
 * AND log redaction of account tokens, so the redaction cases are security-
 * relevant — a regression would drop reset tokens back into the access log.
 */

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "api/RequestUtils.hpp"

namespace {

using Api::normalize_path_for_metrics;

TEST(RequestUtilsTest, PlainPathUnchanged) {
    EXPECT_EQ(normalize_path_for_metrics("/api/v1/jobs"), "/api/v1/jobs");
    EXPECT_EQ(normalize_path_for_metrics("/"), "/");
    EXPECT_EQ(normalize_path_for_metrics("/healthz"), "/healthz");
}

TEST(RequestUtilsTest, UuidSegmentRedacted) {
    EXPECT_EQ(normalize_path_for_metrics("/api/v1/jobs/123e4567-e89b-12d3-a456-426614174000"), "/api/v1/jobs/:id");
    EXPECT_EQ(normalize_path_for_metrics("/api/v1/admin/users/123e4567-e89b-12d3-a456-426614174000"),
              "/api/v1/admin/users/:id");
    // UUID mid-path + trailing segment.
    EXPECT_EQ(normalize_path_for_metrics("/api/v1/jobs/dlq/123e4567-e89b-12d3-a456-426614174000/requeue"),
              "/api/v1/jobs/dlq/:id/requeue");
}

TEST(RequestUtilsTest, AccountTokensRedacted) {
    EXPECT_EQ(normalize_path_for_metrics("/api/v1/account/confirm/eyJhbGciOi.JTV.sig"),
              "/api/v1/account/confirm/:token");
    EXPECT_EQ(normalize_path_for_metrics("/api/v1/account/reset-password/some.long.token"),
              "/api/v1/account/reset-password/:token");
    EXPECT_EQ(normalize_path_for_metrics("/api/v1/account/change-email/some.long.token"),
              "/api/v1/account/change-email/:token");
    // Regression: the invite-redeem token (a 7-day account-takeover secret) must
    // be redacted too — it was leaking into access logs + the metric path label.
    EXPECT_EQ(normalize_path_for_metrics("/api/v1/account/join-from-invite/eyJhbGciOi.JTV.sig"),
              "/api/v1/account/join-from-invite/:token");
}

TEST(RequestUtilsTest, AniHookTokenRedacted) {
    // The webhook token is a credential in the path: it never reaches logs or metric labels.
    EXPECT_EQ(normalize_path_for_metrics("/api/v1/ani/hook/aniwh_0123456789abcdef0123456789abcdef0123456789abcdef"),
              "/api/v1/ani/hook/:token");
    EXPECT_EQ(normalize_path_for_metrics("/api/v1/ani/hook"), "/api/v1/ani/hook");
    EXPECT_EQ(normalize_path_for_metrics("/api/v1/ani/webhook/rotate"), "/api/v1/ani/webhook/rotate");
}

TEST(RequestUtilsTest, RequestVariantsNotMistakenForTokenRoutes) {
    // The *-request / *-resend single-segment routes carry no token.
    EXPECT_EQ(normalize_path_for_metrics("/api/v1/account/reset-password-request"),
              "/api/v1/account/reset-password-request");
    EXPECT_EQ(normalize_path_for_metrics("/api/v1/account/confirm-resend"), "/api/v1/account/confirm-resend");
}

TEST(RequestUtilsTest, IsValidUuid) {
    EXPECT_TRUE(Api::is_valid_uuid("123e4567-e89b-12d3-a456-426614174000"));
    EXPECT_FALSE(Api::is_valid_uuid("not-a-uuid"));
    EXPECT_FALSE(Api::is_valid_uuid("123e4567e89b12d3a456426614174000"));  // no dashes
    EXPECT_FALSE(Api::is_valid_uuid(""));
}

TEST(RequestUtilsTest, ParseIntClamp) {
    EXPECT_EQ(Api::parse_int("42", 7), 42);
    EXPECT_EQ(Api::parse_int("", 7), 7);
    EXPECT_EQ(Api::parse_int("abc", 7), 7);
    EXPECT_EQ(Api::clamp_int("500", 20, 1, 200), 200);
    EXPECT_EQ(Api::clamp_int("-5", 20, 0, 200), 0);
    EXPECT_EQ(Api::clamp_int("", 20, 1, 200), 20);
}

// 2026-10-14 20:30 UTC is already 2026-10-15 in Asia/Almaty (UTC+5): a default
// taken from UTC would file the evening's row under the day before.
drogon::HttpRequestPtr with_params(const std::vector<std::pair<std::string, std::string>>& params) {
    auto req = drogon::HttpRequest::newHttpRequest();
    for (const auto& [k, v] : params) {
        req->setParameter(k, v);
    }
    return req;
}

std::optional<std::string> almaty_only(const std::string& tz) {
    if (tz == "Asia/Almaty") {
        return std::string("2026-10-15");
    }
    return std::nullopt;
}

TEST(RequestUtilsTest, LocalTodayFollowsAValidZone) {
    EXPECT_EQ(Api::local_today(with_params({{"tz", "Asia/Almaty"}}), almaty_only, "2026-10-14"), "2026-10-15");
    EXPECT_EQ(Api::local_today(with_params({}), almaty_only, "2026-10-14"), "2026-10-14") << "no tz: UTC";
    EXPECT_EQ(Api::local_today(with_params({{"tz", "Mars/Olympus"}}), almaty_only, "2026-10-14"), "2026-10-14")
        << "not a zone: UTC";
    const auto down = [](const std::string&) -> std::optional<std::string> {
        throw std::runtime_error("database is away");
    };
    EXPECT_EQ(Api::local_today(with_params({{"tz", "Asia/Almaty"}}), down, "2026-10-14"), "2026-10-14")
        << "a resolver that cannot answer leaves UTC";
}

TEST(RequestUtilsTest, RequestDayPrefersTheGivenDate) {
    EXPECT_EQ(Api::request_day(with_params({{"date", "2026-09-01"}, {"tz", "Asia/Almaty"}}), almaty_only, "2026-10-14"),
              "2026-09-01");
    EXPECT_EQ(Api::request_day(with_params({{"tz", "Asia/Almaty"}}), almaty_only, "2026-10-14"), "2026-10-15");
    EXPECT_EQ(Api::request_day(with_params({}), almaty_only, "2026-10-14"), "2026-10-14");
}

}  // namespace
