/**
 * @file test_field_checks.cpp
 * @brief The money amount check in src/api/FieldChecks.hpp: significant
 *        digits of a JSON number and the 15-digit cap.
 */

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
