/**
 * @file test_shared_utils.cpp
 * @brief The shared encoders and date helpers in utils/ that replaced the
 *        per-module copies: percent-encoding, ASCII lowercase, UUID shape,
 *        hex, standard base64, YYYY-MM-DD, RFC 3339 and the UTF-8 byte cut,
 *        plus the Mi region day offset. Pure, no services.
 */

#include <chrono>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "fitness/xiaomi/Regions.hpp"
#include "utils/Base64.hpp"
#include "utils/Crypto.hpp"
#include "utils/Date.hpp"
#include "utils/Strings.hpp"
#include "utils/Time.hpp"
#include "utils/Utf8.hpp"

namespace {

TEST(SharedUtilsStrings, PercentEncodeKeepsOnlyTheUnreservedSet) {
    using Utils::Strings::percent_encode;
    EXPECT_EQ(percent_encode("AZaz09-_.~"), "AZaz09-_.~");
    EXPECT_EQ(percent_encode("a b&c=d+e"), "a%20b%26c%3Dd%2Be");
    EXPECT_EQ(percent_encode("a/b"), "a%2Fb");
    EXPECT_EQ(percent_encode("a/b c", true), "a/b%20c");
    EXPECT_EQ(percent_encode("\xD0\x90"), "%D0%90") << "UTF-8 bytes, upper-case hex";
    EXPECT_EQ(percent_encode(std::string("\x00\xff", 2)), "%00%FF");
    EXPECT_EQ(percent_encode(""), "");
}

TEST(SharedUtilsStrings, AsciiLowerTouchesOnlyAsciiLetters) {
    using Utils::Strings::ascii_lower;
    EXPECT_EQ(ascii_lower("Set-Cookie"), "set-cookie");
    EXPECT_EQ(ascii_lower("ABC xyz 019"), "abc xyz 019");
    EXPECT_EQ(ascii_lower("\xC3\x84"), "\xC3\x84") << "UTF-8 bytes stay as they are";
    EXPECT_EQ(ascii_lower(""), "");
}

TEST(SharedUtilsStrings, IsUuidChecksTheEightFourFourFourTwelveShape) {
    using Utils::Strings::is_uuid;
    EXPECT_TRUE(is_uuid("123e4567-e89b-12d3-a456-426614174000"));
    EXPECT_TRUE(is_uuid("123E4567-E89B-12D3-A456-426614174000"));
    EXPECT_FALSE(is_uuid("123e4567-e89b-12d3-a456-42661417400"));
    EXPECT_FALSE(is_uuid("123e4567-e89b-12d3-a456-4266141740000"));
    EXPECT_FALSE(is_uuid("123e4567e89b-12d3-a456-4266141740000"));
    EXPECT_FALSE(is_uuid("g23e4567-e89b-12d3-a456-426614174000"));
    EXPECT_FALSE(is_uuid(""));
}

TEST(SharedUtilsCrypto, ToHexIsLowerCaseAndMatchesTheOldName) {
    const std::string bytes("\x00\xff\x10\xab", 4);
    EXPECT_EQ(Utils::Crypto::to_hex(bytes), "00ff10ab");
    EXPECT_EQ(Utils::Crypto::detail::bytes_to_hex(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()),
              "00ff10ab");
    EXPECT_EQ(Utils::Crypto::to_hex(std::string()), "");
}

TEST(SharedUtilsBase64, StandardEncodePadsAndUsesPlusSlash) {
    using Utils::Base64::encode;
    EXPECT_EQ(encode(""), "");
    EXPECT_EQ(encode("A"), "QQ==");
    EXPECT_EQ(encode("ab"), "YWI=");
    EXPECT_EQ(encode("abc"), "YWJj");
    EXPECT_EQ(encode(std::string("\xff\xfe", 2)), "//4=");
    EXPECT_EQ(encode("client:secret"), "Y2xpZW50OnNlY3JldA==");
}

TEST(SharedUtilsBase64, StandardDecodeRoundTripsAndIsStrict) {
    using Utils::Base64::decode;
    using Utils::Base64::encode;
    for (const std::string& s : {std::string(""),
                                 std::string("a"),
                                 std::string("ab"),
                                 std::string("abc"),
                                 std::string("hello world"),
                                 std::string("\x00\x01\x02\xff\xfe", 5)}) {
        EXPECT_EQ(decode(encode(s)), s) << "roundtrip failed for size " << s.size();
    }
    EXPECT_EQ(decode("//4="), std::string("\xff\xfe", 2));
    EXPECT_THROW(decode("QQ="), std::runtime_error);
    EXPECT_THROW(decode("QQ==QQ=="), std::runtime_error);
    EXPECT_THROW(decode("Q==="), std::runtime_error);
    EXPECT_THROW(decode("not base64!!"), std::runtime_error);
    EXPECT_THROW(decode("YW-_"), std::runtime_error) << "the url alphabet is not standard base64";
}

TEST(SharedUtilsDate, FormatsAndShiftsDays) {
    using namespace std::chrono;
    EXPECT_EQ(Utils::Date::format_ymd(year_month_day{year{2026}, month{2}, day{3}}), "2026-02-03");
    EXPECT_EQ(Utils::Date::format_ymd(sys_days{}), "1970-01-01");
    EXPECT_EQ(Utils::Date::format_ymd(year_month_day{year{987}, month{12}, day{31}}), "0987-12-31");
    EXPECT_EQ(Utils::Date::add_days("2026-02-28", 1), "2026-03-01");
    EXPECT_EQ(Utils::Date::add_days("2024-02-28", 1), "2024-02-29");
    EXPECT_EQ(Utils::Date::add_days("2026-01-01", -1), "2025-12-31");
    EXPECT_EQ(Utils::Date::add_days("2026-10-29", 0), "2026-10-29");
    EXPECT_THROW(Utils::Date::add_days("2026-02-30", 1), std::invalid_argument);
    EXPECT_THROW(Utils::Date::add_days("bad", 1), std::invalid_argument);
    EXPECT_TRUE(Utils::Date::is_ymd(Utils::Date::today_utc()));
}

TEST(SharedUtilsTime, ParsesRfc3339) {
    using Utils::Time::parse_rfc3339;
    EXPECT_EQ(parse_rfc3339("1970-01-01T00:00:00Z"), 0);
    EXPECT_EQ(parse_rfc3339("1970-01-01t00:00:01z"), 1);
    EXPECT_EQ(parse_rfc3339("2026-10-29T10:00:00Z"), 1793268000);
    EXPECT_EQ(parse_rfc3339("2026-10-29T15:00:00+05:00"), 1793268000);
    EXPECT_EQ(parse_rfc3339("2026-10-29T06:00:00-04:00"), 1793268000);
    EXPECT_EQ(parse_rfc3339("2026-10-29T10:00:00.123Z"), 1793268000);
    EXPECT_FALSE(parse_rfc3339("2026-10-29").has_value());
    EXPECT_FALSE(parse_rfc3339("2026-10-29T10:00:00").has_value());
    EXPECT_FALSE(parse_rfc3339("2026-13-29T10:00:00Z").has_value());
    EXPECT_FALSE(parse_rfc3339("2026-10-29T10:00:00Zx").has_value());
    EXPECT_FALSE(parse_rfc3339("2026-10-29T10:00:00+05").has_value());
    EXPECT_EQ(Utils::Time::epoch_to_iso8601(1793268000), "2026-10-29T10:00:00Z");
}

TEST(SharedUtilsUtf8, PrefixBytesNeverCutsACharacter) {
    using Utils::Utf8::prefix_bytes;
    EXPECT_EQ(prefix_bytes("abc", 5), "abc");
    EXPECT_EQ(prefix_bytes("abc", 2), "ab");
    // "Пр" is D0 9F D1 80: three bytes would end inside the second letter.
    EXPECT_EQ(prefix_bytes("\xD0\x9F\xD1\x80", 3), "\xD0\x9F");
    EXPECT_EQ(prefix_bytes("\xD0\x9F\xD1\x80", 4), "\xD0\x9F\xD1\x80");
    EXPECT_EQ(prefix_bytes("\xD0\x9F", 1), "");
}

TEST(SharedUtilsMiRegion, CnAndEmptyAreUtcPlusEight) {
    EXPECT_TRUE(Xiaomi::is_cn(""));
    EXPECT_TRUE(Xiaomi::is_cn("cn"));
    EXPECT_FALSE(Xiaomi::is_cn("de"));
    EXPECT_EQ(Xiaomi::offset_seconds(""), 8 * 3600);
    EXPECT_EQ(Xiaomi::offset_seconds("cn"), 8 * 3600);
    EXPECT_EQ(Xiaomi::offset_seconds("sg"), 0);
    EXPECT_EQ(Xiaomi::offset_seconds("ru"), 0);
}

}  // namespace
