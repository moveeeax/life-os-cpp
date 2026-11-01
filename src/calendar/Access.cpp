/**
 * @file Access.cpp
 * @brief Body for src/calendar/Access.hpp.
 */

#include "calendar/Access.hpp"

#include <chrono>
#include <ctime>
#include <exception>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "repositories/calendar/LinkRepository.hpp"
#include "utils/Seal.hpp"

namespace Calendar {

namespace {

struct Cached {
    std::string token;
    /// The sealed refresh token it came from: a link replaced or removed on another replica no longer matches.
    std::string sealed;
    std::chrono::steady_clock::time_point until;
};

std::mutex& lock() {
    static std::mutex m;
    return m;
}

std::map<std::string, Cached>& cache() {
    static std::map<std::string, Cached> c;
    return c;
}

/// Pages of 250 events read at most for one range (a busy 31 days).
constexpr int kMaxPages = 4;

}  // namespace

void forget(const std::string& owner) {
    std::lock_guard<std::mutex> g(lock());
    cache().erase(owner);
}

std::variant<std::string, Problem> access(Net::Http::Transport& t, const std::string& owner) {
    const auto s = settings();
    if (!s) {
        return Problem{404, "calendar_not_configured"};
    }
    // The link is read every time (one row): the cache is per process, and a Disconnect or a new
    // Connect may have gone through another replica.
    Repositories::Calendar::LinkRepository links;
    const auto link = links.get(owner);
    if (!link || link->broken) {
        forget(owner);
        return link ? Problem{409, "calendar_disconnected"} : Problem{404, "calendar_not_linked"};
    }
    {
        std::lock_guard<std::mutex> g(lock());
        const auto it = cache().find(owner);
        if (it != cache().end() && it->second.sealed == link->sealed &&
            it->second.until > std::chrono::steady_clock::now()) {
            return it->second.token;
        }
    }
    std::string refresh_token;
    try {
        refresh_token = Utils::Seal::unseal({link->sealed, link->nonce}, s->token_key);
    } catch (const Utils::Seal::SealError& e) {
        spdlog::warn("calendar: the refresh token of a user does not open: {}", e.what());
        links.mark_broken(owner);
        return Problem{409, "calendar_disconnected"};
    }
    const auto tokens = refresh(t, *s, refresh_token);
    if (tokens.error == "invalid_grant") {
        if (links.mark_broken(owner)) {
            spdlog::info("calendar: Google refused the refresh; the link is marked broken");
        }
        return Problem{409, "calendar_disconnected"};
    }
    if (!tokens.ok()) {
        return Problem{502, "calendar_unavailable"};
    }
    const long long life = tokens.expires_in > 120 ? tokens.expires_in - 60 : 60;
    std::lock_guard<std::mutex> g(lock());
    cache()[owner] =
        Cached{tokens.access_token, link->sealed, std::chrono::steady_clock::now() + std::chrono::seconds(life)};
    return tokens.access_token;
}

std::string rfc3339(long long t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::variant<std::vector<Event>, Problem> events(
    Net::Http::Transport& t, const std::string& owner, long long from, long long to, const std::string& tz) {
    auto token = access(t, owner);
    if (auto* p = std::get_if<Problem>(&token)) {
        return *p;
    }
    // A busy month fills more than one page: the pages are read up to kMaxPages and joined.
    nlohmann::json items = nlohmann::json::array();
    std::string page;
    for (int n = 0; n < kMaxPages; ++n) {
        std::vector<std::pair<std::string, std::string>> query{{"timeMin", rfc3339(from)},
                                                               {"timeMax", rfc3339(to)},
                                                               {"singleEvents", "true"},
                                                               {"orderBy", "startTime"},
                                                               {"maxResults", "250"}};
        if (!page.empty()) {
            query.emplace_back("pageToken", page);
        }
        const auto r = api(t, std::get<std::string>(token), "GET", "/calendars/primary/events", query);
        if (r.status == 401) {
            forget(owner);
            return Problem{502, "calendar_unavailable"};
        }
        if (!r.ok() || !r.body.is_object()) {
            return Problem{502, "calendar_unavailable"};
        }
        for (const auto& item : r.body.value("items", nlohmann::json::array())) {
            items.push_back(item);
        }
        const auto next = r.body.find("nextPageToken");
        if (next == r.body.end() || !next->is_string() || next->get<std::string>().empty()) {
            break;
        }
        page = next->get<std::string>();
    }
    Repositories::Calendar::LinkRepository links;
    // One query per distinct day, not two per all-day event.
    std::map<std::string, long long> day_starts;
    return parse_events(nlohmann::json{{"items", std::move(items)}}, [&](const std::string& date) {
        const auto it = day_starts.find(date);
        if (it != day_starts.end()) {
            return it->second;
        }
        const long long at = links.local_instant(date, 0, tz);
        day_starts.emplace(date, at);
        return at;
    });
}

}  // namespace Calendar
