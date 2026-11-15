/**
 * @file Time.hpp
 * @brief Epoch-time helpers shared across modules.
 *
 * Centralizes the `duration_cast<...>(system_clock::now().time_since_epoch())`
 * idiom that was previously open-coded in Auth, Jobs, Tokens, RateLimit and
 * the auth controllers. One source of truth for "what time is it (epoch)".
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>

namespace Utils::Time {

/// Seconds since the Unix epoch.
inline std::int64_t now_epoch_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// Milliseconds since the Unix epoch.
inline std::int64_t now_epoch_millis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// "2026-07-25 05:34:32.87643+00[:00]" (libpqxx text form) → ISO 8601.
/// Space→'T', fractional seconds dropped, "+00"/"+00:00"→"Z"; other offsets
/// kept (still valid ISO 8601). Inputs that don't look like
/// "YYYY-MM-DD HH:MM:SS..." are returned unchanged.
inline std::string pg_to_iso8601(const std::string& ts) {
    if (ts.size() < 19 || ts[10] != ' ')
        return ts;
    std::string out = ts.substr(0, 10) + "T" + ts.substr(11, 8);
    std::size_t i = 19;
    if (i < ts.size() && ts[i] == '.') {  // fractional seconds
        ++i;
        while (i < ts.size() && ts[i] >= '0' && ts[i] <= '9')
            ++i;
    }
    const std::string offset = ts.substr(i);
    if (offset == "+00" || offset == "+00:00" || offset == "Z" || offset.empty())
        out += "Z";
    else
        out += offset;
    return out;
}

/// Unix seconds → "YYYY-MM-DDTHH:MM:SSZ" (UTC).
inline std::string epoch_to_iso8601(std::int64_t secs) {
    const std::time_t t = static_cast<std::time_t>(secs);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[24];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

/**
 * @brief Unix seconds of an RFC 3339 date-time: YYYY-MM-DDTHH:MM:SS, optional
 *        fractional seconds (dropped), then Z or a +HH:MM / -HH:MM offset.
 *        'T' and 'Z' may be lower case. Anything else, trailing text
 *        included, is nullopt. The day is checked against 1..31 only, as the
 *        calendar and GitHub parsers did: a 31st of a short month rolls over.
 */
inline std::optional<std::int64_t> parse_rfc3339(std::string_view s) {
    const auto digits = [&](std::size_t at, std::size_t n, int& out) {
        if (at + n > s.size()) {
            return false;
        }
        int v = 0;
        for (std::size_t i = at; i < at + n; ++i) {
            if (s[i] < '0' || s[i] > '9') {
                return false;
            }
            v = v * 10 + (s[i] - '0');
        }
        out = v;
        return true;
    };
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (!digits(0, 4, y) || s.size() < 19 || s[4] != '-' || !digits(5, 2, mo) || s[7] != '-' || !digits(8, 2, d) ||
        (s[10] != 'T' && s[10] != 't') || !digits(11, 2, h) || s[13] != ':' || !digits(14, 2, mi) || s[16] != ':' ||
        !digits(17, 2, se) || mo < 1 || mo > 12 || d < 1 || d > 31) {
        return std::nullopt;
    }
    std::size_t at = 19;
    if (at < s.size() && s[at] == '.') {
        ++at;
        while (at < s.size() && s[at] >= '0' && s[at] <= '9') {
            ++at;
        }
    }
    long long offset = 0;
    if (at < s.size() && (s[at] == 'Z' || s[at] == 'z')) {
        ++at;
    } else if (at < s.size() && (s[at] == '+' || s[at] == '-')) {
        int oh = 0, om = 0;
        if (!digits(at + 1, 2, oh) || at + 3 >= s.size() || s[at + 3] != ':' || !digits(at + 4, 2, om)) {
            return std::nullopt;
        }
        offset = (oh * 3600LL + om * 60LL) * (s[at] == '-' ? -1 : 1);
        at += 6;
    } else {
        return std::nullopt;
    }
    if (at != s.size()) {
        return std::nullopt;
    }
    // Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm).
    long long yy = y;
    yy -= mo <= 2;
    const long long era = (yy >= 0 ? yy : yy - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(yy - era * 400);
    const unsigned m = static_cast<unsigned>(mo);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long long days = era * 146097 + static_cast<long long>(doe) - 719468;
    return static_cast<std::int64_t>(days * 86400LL + h * 3600LL + mi * 60LL + se - offset);
}

}  // namespace Utils::Time
