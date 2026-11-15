/**
 * @file test_field_checks.cpp
 * @brief The checks in src/api/FieldChecks.hpp: significant
 *        digits of a JSON number, the 15-digit cap, opt_int not narrowing,
 *        text_or_empty refusing another type, a time of day, a currency
 *        code, the tri-state patch reader and the ?date= of a route.
 */

#include <optional>
#include <string>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/FieldChecks.hpp"

using Api::Fields::amount_range;
using Api::Fields::significant_digits;
using json = nlohmann::json;

TEST(FieldChecksAmount, SignificantDigitsIgnoreZerosSignPointAndExponent) {
    EXPECT_EQ(significant_digits("0"), 0);
    EXPECT_EQ(significant_digits("0.0"), 0);
    EXPECT_EQ(significant_digits("13275.61"), 7);
    EXPECT_EQ(significant_digits("-0.00001234"), 4);
    EXPECT_EQ(significant_digits("1000000000000"), 1);
    EXPECT_EQ(significant_digits("1e-08"), 1);
    EXPECT_EQ(significant_digits("1.2345e+20"), 5);
    EXPECT_EQ(significant_digits("0.30000000000000004"), 17);
}

TEST(FieldChecksAmount, TheDumpOfADoubleIsItsShortestForm) {
    EXPECT_EQ(significant_digits(json(0.1).dump()), 1);
    EXPECT_EQ(significant_digits(json(13275.61).dump()), 7);
    EXPECT_EQ(significant_digits(json(0.1 + 0.2).dump()), 17);
    EXPECT_EQ(significant_digits(json(123456789012.34).dump()), 14);
}

TEST(FieldChecksAmount, MoreThanFifteenDigitsIsRefused) {
    const auto check = [](const json& value) {
        Api::Validation::Errors errs;
        amount_range(errs, json{{"amount", value}}, "amount", -1e12, 1e12);
        return errs.items().empty() ? std::string() : errs.items()[0].code;
    };
    EXPECT_EQ(check(13275.61), "");
    EXPECT_EQ(check(123456789012.345), "") << "15 digits";
    EXPECT_EQ(check(12345678901.23456), "precision") << "16 digits";
    EXPECT_EQ(check(0.1 + 0.2), "precision");
    EXPECT_EQ(check(nullptr), "") << "null is unset";
    EXPECT_EQ(check(2e12), "out_of_range") << "one error, not two";
    EXPECT_EQ(check("12"), "not_number");
}

TEST(FieldChecksInt, AnIntegerPastTheIntRangeIsNotNarrowed) {
    using Api::Fields::opt_int;
    EXPECT_EQ(opt_int(json{{"d", 30}}, "d"), 30);
    EXPECT_EQ(opt_int(json{{"d", -5}}, "d"), -5);
    EXPECT_FALSE(opt_int(json{{"d", 4294967297LL}}, "d").has_value()) << "2^32+1 would narrow to 1";
    EXPECT_FALSE(opt_int(json{{"d", 18446744073709551615ULL}}, "d").has_value());
    EXPECT_FALSE(opt_int(json{{"d", -2147483649LL}}, "d").has_value());
    EXPECT_EQ(opt_int(json{{"d", 2147483647}}, "d"), 2147483647);
    EXPECT_FALSE(opt_int(json{{"d", 1.5}}, "d").has_value());
    EXPECT_FALSE(opt_int(json::object(), "d").has_value());
}

TEST(FieldChecksText, AnotherTypeIsRefusedNotThrown) {
    using Api::Fields::text_or_empty;
    EXPECT_EQ(text_or_empty(json{{"t", "x"}}, "t"), "x");
    EXPECT_EQ(text_or_empty(json::object(), "t"), "") << "absent";
    EXPECT_EQ(text_or_empty(json{{"t", nullptr}}, "t"), "") << "null is unset";
    EXPECT_FALSE(text_or_empty(json{{"t", 42}}, "t").has_value());
    EXPECT_FALSE(text_or_empty(json{{"t", false}}, "t").has_value());
    EXPECT_FALSE(text_or_empty(json{{"t", json::array()}}, "t").has_value());
}

TEST(FieldChecksTime, HhMmWithAnOptionalOneDigitHour) {
    using Api::Fields::is_hhmm;
    EXPECT_TRUE(is_hhmm("00:00"));
    EXPECT_TRUE(is_hhmm("23:59"));
    EXPECT_FALSE(is_hhmm("24:00"));
    EXPECT_FALSE(is_hhmm("12:60"));
    EXPECT_FALSE(is_hhmm("8:30")) << "two digits by default";
    EXPECT_TRUE(is_hhmm("8:30", /*allow_single_digit_hour=*/true));
    EXPECT_FALSE(is_hhmm("", true));
}

TEST(FieldChecksCurrency, ThreeUpperCaseLettersAndTheCallersExample) {
    using Api::Fields::currency_code;
    Api::Validation::Errors ok;
    currency_code(ok, json{{"c", "KZT"}}, "c", "KZT");
    currency_code(ok, json{{"c", nullptr}}, "c", "KZT");
    currency_code(ok, json::object(), "c", "KZT");
    EXPECT_FALSE(ok.any()) << "a code, null and absent all pass";
    Api::Validation::Errors bad;
    currency_code(bad, json{{"c", "usd"}}, "c", "USD");
    currency_code(bad, json{{"d", 840}}, "d", "USD");
    ASSERT_EQ(bad.items().size(), 2u);
    EXPECT_EQ(bad.items()[0].code, "bad_format");
    EXPECT_EQ(bad.items()[0].message, "expected an ISO code like USD");
}

TEST(FieldChecksPatch, AbsentNullAndText) {
    using Api::Fields::patch_text;
    EXPECT_FALSE(patch_text(json::object(), "f").has_value()) << "absent: leave it";
    const auto cleared = patch_text(json{{"f", nullptr}}, "f");
    ASSERT_TRUE(cleared.has_value());
    EXPECT_FALSE(cleared->has_value()) << "null: clear it";
    const auto set = patch_text(json{{"f", "x"}}, "f");
    ASSERT_TRUE(set.has_value() && set->has_value());
    EXPECT_EQ(**set, "x");
}

namespace {

drogon::HttpRequestPtr with_date(const std::string& date) {
    auto req = drogon::HttpRequest::newHttpRequest();
    if (!date.empty()) {
        req->setParameter("date", date);
    }
    return req;
}

std::optional<std::string> no_zone(const std::string&) {
    return std::nullopt;
}

}  // namespace

TEST(FieldChecksDay, TheGivenDayOrTodayAndA400ForAnImpossibleOne) {
    drogon::HttpResponsePtr answered;
    const auto cb = [&](const drogon::HttpResponsePtr& r) { answered = r; };
    EXPECT_EQ(Api::Fields::day_param(with_date("2026-02-28"), cb, no_zone), "2026-02-28");
    EXPECT_FALSE(answered);
    EXPECT_EQ(Api::Fields::day_param(with_date(""), cb, no_zone), Api::Fields::today_utc());
    EXPECT_FALSE(answered);
    EXPECT_FALSE(Api::Fields::day_param(with_date("2026-02-30"), cb, no_zone).has_value());
    ASSERT_TRUE(answered);
    EXPECT_EQ(answered->statusCode(), drogon::k400BadRequest);
    EXPECT_EQ(json::parse(std::string(answered->body()))["error"], "invalid_query");
}
