/**
 * @file Google.hpp
 * @brief Google Calendar as Life OS reaches it: the OAuth Web client of the
 *        site (calendar.google.*), the consent URL, the code exchange, the
 *        refresh of an access token, the revoke, and the events of the
 *        primary calendar. A refresh token is kept sealed per user
 *        (repositories/calendar); an access token lives in memory only.
 *        The pure parts (parsing, free slots) carry the tests.
 */

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "net/Http.hpp"

namespace Calendar {

struct Settings {
    std::string client_id;
    std::string client_secret;
    std::string redirect_url;
    /// The seal key of refresh tokens (utils/Seal.hpp).
    std::string token_key;
};

/// The settings; nullopt while the client id, secret or key is missing.
std::optional<Settings> settings();

/// Events only: the site reads and changes events of the primary calendar, never calendars or sharing.
inline constexpr const char* kScope = "https://www.googleapis.com/auth/calendar.events openid email";

/// The consent URL for @p state (offline access, consent every time so a refresh token comes back).
std::string auth_url(const Settings& s, const std::string& state);

/// What a token call gave: the tokens, or the error Google named ("invalid_grant" when refused).
struct Tokens {
    std::string access_token;
    std::string refresh_token;
    long long expires_in = 0;
    std::string id_email;
    std::string error;
    bool ok() const { return error.empty() && !access_token.empty(); }
};

Tokens exchange(Net::Http::Transport& t, const Settings& s, const std::string& code);
Tokens refresh(Net::Http::Transport& t, const Settings& s, const std::string& refresh_token);
/// Best effort: false when Google did not take it.
bool revoke(Net::Http::Transport& t, const std::string& token);

struct Event {
    std::string id;
    std::string title;
    /// Unix seconds; an all-day event spans its local days, from 00:00.
    long long start = 0;
    long long end = 0;
    bool all_day = false;
    /// Free time per Google (transparency "transparent"), or a birthday.
    bool free = false;
    std::string location;
    /// The start and end as Google wrote them (dateTime with offset, or a date).
    std::string start_text;
    std::string end_text;
};

/// RFC 3339 ("2026-10-29T11:00:00+05:00", "...Z", fractions) to unix seconds; nullopt when it is not one.
std::optional<long long> parse_time(const std::string& s);

/// The events of a Google events list; all-day dates become instants with @p day_start(date) (local midnight).
std::vector<Event> parse_events(const nlohmann::json& list,
                                const std::function<long long(const std::string&)>& day_start);

/// Free slots between @p from and @p to (unix seconds) of at least @p min_minutes, around busy events.
std::vector<std::pair<long long, long long>> free_slots(const std::vector<Event>& events,
                                                        long long from,
                                                        long long to,
                                                        int min_minutes);

/// The view of an event for the site and Ani.
nlohmann::json event_view(const Event& e);

/// One Calendar API call with a bearer token: method, path under /calendar/v3, query and body.
struct Reply {
    long status = 0;
    nlohmann::json body;
    bool ok() const { return status >= 200 && status < 300; }
};
Reply api(Net::Http::Transport& t,
          const std::string& access_token,
          const std::string& method,
          const std::string& path,
          const std::vector<std::pair<std::string, std::string>>& query = {},
          const nlohmann::json& body = nullptr);

/// @p s percent-encoded for a query value or a form field.
std::string encode(const std::string& s);

}  // namespace Calendar
