/**
 * @file Google.cpp
 * @brief Body for src/calendar/Google.hpp.
 */

#include "calendar/Google.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <exception>

#include "utils/Base64.hpp"
#include "utils/Config.hpp"

namespace Calendar {

namespace {

using json = nlohmann::json;

constexpr const char* kTokenUrl = "https://oauth2.googleapis.com/token";
constexpr const char* kApiBase = "https://www.googleapis.com/calendar/v3";

/// Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm).
long long days_from_civil(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

bool digits(const std::string& s, std::size_t at, std::size_t n, int& out) {
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
}

std::string iso_utc(long long t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

Tokens tokens_of(const Net::Http::Response& r) {
    Tokens out;
    const json j = json::parse(r.body, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object()) {
        out.error = r.status == 0 ? "unavailable" : "bad_answer";
        return out;
    }
    if (r.status < 200 || r.status >= 300) {
        out.error = j.value("error", std::string("http_" + std::to_string(r.status)));
        return out;
    }
    out.access_token = j.value("access_token", std::string());
    out.refresh_token = j.value("refresh_token", std::string());
    out.expires_in = j.value("expires_in", 0LL);
    // The id_token's payload names the account; it came over TLS from Google itself.
    const std::string id = j.value("id_token", std::string());
    const auto a = id.find('.');
    const auto b = a == std::string::npos ? a : id.find('.', a + 1);
    if (b != std::string::npos) {
        try {
            const json claims = json::parse(Utils::Base64::url_decode(id.substr(a + 1, b - a - 1)));
            out.id_email = claims.value("email", std::string());
        } catch (const std::exception&) {}
    }
    if (out.access_token.empty()) {
        out.error = "no_access_token";
    }
    return out;
}

Tokens token_call(Net::Http::Transport& t, const std::string& form) {
    Net::Http::Request r;
    r.method = "POST";
    r.url = kTokenUrl;
    r.body = form;
    r.content_type = "application/x-www-form-urlencoded";
    r.headers = {{"Accept", "application/json"}};
    r.timeout_seconds = 15;
    try {
        return tokens_of(t.send(r));
    } catch (const Net::Http::TransportError&) {
        Tokens out;
        out.error = "unavailable";
        return out;
    }
}

}  // namespace

std::optional<Settings> settings() {
    if (!Config::is_initialized()) {
        return std::nullopt;
    }
    auto& cfg = Config::get();
    Settings s;
    s.client_id = cfg.get<std::string>("calendar.google.client_id", "GOOGLE_CALENDAR_CLIENT_ID", "");
    s.client_secret = cfg.get<std::string>("calendar.google.client_secret", "GOOGLE_CALENDAR_CLIENT_SECRET", "");
    s.redirect_url = cfg.get<std::string>("calendar.google.redirect_url",
                                          "GOOGLE_CALENDAR_REDIRECT_URL",
                                          "https://life-os.tarassov.me/api/v1/calendar/oauth/callback");
    s.token_key = cfg.get<std::string>("calendar.google.token_key", "CALENDAR_TOKEN_KEY", "");
    if (s.client_id.empty() || s.client_secret.empty() || s.token_key.empty() || s.redirect_url.empty()) {
        return std::nullopt;
    }
    return s;
}

std::string encode(const std::string& s) {
    std::string out;
    for (const unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

std::string auth_url(const Settings& s, const std::string& state) {
    return "https://accounts.google.com/o/oauth2/v2/auth?client_id=" + encode(s.client_id) +
           "&redirect_uri=" + encode(s.redirect_url) + "&response_type=code&scope=" + encode(kScope) +
           "&access_type=offline&prompt=consent&include_granted_scopes=true&state=" + encode(state);
}

Tokens exchange(Net::Http::Transport& t, const Settings& s, const std::string& code) {
    return token_call(t,
                      "grant_type=authorization_code&code=" + encode(code) + "&client_id=" + encode(s.client_id) +
                          "&client_secret=" + encode(s.client_secret) + "&redirect_uri=" + encode(s.redirect_url));
}

Tokens refresh(Net::Http::Transport& t, const Settings& s, const std::string& refresh_token) {
    return token_call(t,
                      "grant_type=refresh_token&refresh_token=" + encode(refresh_token) +
                          "&client_id=" + encode(s.client_id) + "&client_secret=" + encode(s.client_secret));
}

bool revoke(Net::Http::Transport& t, const std::string& token) {
    Net::Http::Request r;
    r.method = "POST";
    r.url = "https://oauth2.googleapis.com/revoke";
    r.body = "token=" + encode(token);
    r.content_type = "application/x-www-form-urlencoded";
    r.timeout_seconds = 10;
    try {
        const auto res = t.send(r);
        return res.status >= 200 && res.status < 300;
    } catch (const Net::Http::TransportError&) {
        return false;
    }
}

std::optional<long long> parse_time(const std::string& s) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (!digits(s, 0, 4, y) || s.size() < 19 || s[4] != '-' || !digits(s, 5, 2, mo) || s[7] != '-' ||
        !digits(s, 8, 2, d) || (s[10] != 'T' && s[10] != 't') || !digits(s, 11, 2, h) || s[13] != ':' ||
        !digits(s, 14, 2, mi) || s[16] != ':' || !digits(s, 17, 2, se) || mo < 1 || mo > 12 || d < 1 || d > 31) {
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
        if (!digits(s, at + 1, 2, oh) || at + 3 >= s.size() || s[at + 3] != ':' || !digits(s, at + 4, 2, om)) {
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
    return days_from_civil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400LL + h * 3600LL + mi * 60LL +
           se - offset;
}

std::vector<Event> parse_events(const json& list, const std::function<long long(const std::string&)>& day_start) {
    std::vector<Event> out;
    for (const auto& item : list.value("items", json::array())) {
        if (!item.is_object() || item.value("status", "") == "cancelled") {
            continue;
        }
        const json start = item.value("start", json::object());
        const json end = item.value("end", json::object());
        Event e;
        e.id = item.value("id", std::string());
        e.title = item.value("summary", std::string("(no title)"));
        e.location = item.value("location", std::string());
        e.free = item.value("transparency", "") == "transparent";
        if (start.contains("dateTime")) {
            const auto a = parse_time(start.value("dateTime", ""));
            const auto b = parse_time(end.value("dateTime", ""));
            if (!a || !b) {
                continue;
            }
            e.start = *a;
            e.end = std::max(*a, *b);
            e.start_text = start.value("dateTime", "");
            e.end_text = end.value("dateTime", "");
        } else if (start.contains("date")) {
            e.all_day = true;
            // Busy unless Google says free; a birthday never blocks a day.
            e.free = e.free || item.value("eventType", "") == "birthday";
            e.start_text = start.value("date", "");
            e.end_text = end.value("date", e.start_text);
            e.start = day_start ? day_start(e.start_text) : 0;
            e.end = day_start ? day_start(e.end_text) : 0;
        } else {
            continue;
        }
        if (!e.id.empty()) {
            out.push_back(std::move(e));
        }
    }
    std::sort(out.begin(), out.end(), [](const Event& a, const Event& b) { return a.start < b.start; });
    return out;
}

std::vector<std::pair<long long, long long>> free_slots(const std::vector<Event>& events,
                                                        long long from,
                                                        long long to,
                                                        int min_minutes) {
    std::vector<std::pair<long long, long long>> busy;
    for (const auto& e : events) {
        if (!e.free && e.end > from && e.start < to) {
            busy.emplace_back(std::max(e.start, from), std::min(e.end, to));
        }
    }
    std::sort(busy.begin(), busy.end());
    std::vector<std::pair<long long, long long>> out;
    long long at = from;
    const long long need = static_cast<long long>(std::max(min_minutes, 1)) * 60;
    for (const auto& [a, b] : busy) {
        if (a - at >= need) {
            out.emplace_back(at, a);
        }
        at = std::max(at, b);
    }
    if (to - at >= need) {
        out.emplace_back(at, to);
    }
    return out;
}

json event_view(const Event& e) {
    return json{{"id", e.id},
                {"title", e.title},
                {"start", e.all_day ? e.start_text : iso_utc(e.start)},
                {"end", e.all_day ? e.end_text : iso_utc(e.end)},
                {"all_day", e.all_day},
                {"location", e.location}};
}

Reply api(Net::Http::Transport& t,
          const std::string& access_token,
          const std::string& method,
          const std::string& path,
          const std::vector<std::pair<std::string, std::string>>& query,
          const json& body) {
    Net::Http::Request r;
    r.method = method;
    r.url = std::string(kApiBase) + path;
    char sep = '?';
    for (const auto& [k, v] : query) {
        r.url += sep + k + "=" + encode(v);
        sep = '&';
    }
    r.headers = {{"Authorization", "Bearer " + access_token}, {"Accept", "application/json"}};
    if (!body.is_null()) {
        r.body = body.dump();
        r.content_type = "application/json";
    }
    r.timeout_seconds = 15;
    Reply out;
    try {
        const auto res = t.send(r);
        out.status = res.status;
        out.body = json::parse(res.body, nullptr, /*allow_exceptions=*/false);
        if (out.body.is_discarded()) {
            out.body = nullptr;
        }
    } catch (const Net::Http::TransportError&) {
        out.status = 0;
    }
    return out;
}

}  // namespace Calendar
