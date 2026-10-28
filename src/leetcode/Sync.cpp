/**
 * @file Sync.cpp
 * @brief Body for src/leetcode/Sync.hpp.
 */

#include "leetcode/Sync.hpp"

#include <exception>

#include <spdlog/spdlog.h>

#include "net/Http.hpp"
#include "repositories/leetcode/ProfileRepository.hpp"

namespace LeetCode {

namespace {

using json = nlohmann::json;

constexpr const char* kQuery =
    "query($u:String!){ matchedUser(username:$u){ submitStatsGlobal{ acSubmissionNum{ difficulty count } } } }";

}  // namespace

bool valid_username(std::string_view u) {
    if (u.empty() || u.size() > 30) {
        return false;
    }
    for (const char c : u) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '.' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::optional<Stats> parse_stats(const std::string& body) {
    const json j = json::parse(body, nullptr, false);
    if (!j.is_object() || !j.contains("data") || !j["data"].is_object()) {
        return std::nullopt;
    }
    const json& user = j["data"].value("matchedUser", json());
    if (!user.is_object()) {
        return std::nullopt;
    }
    const json list = user.value("submitStatsGlobal", json::object()).value("acSubmissionNum", json::array());
    if (!list.is_array() || list.empty()) {
        return std::nullopt;
    }
    Stats s;
    bool seen = false;
    for (const auto& e : list) {
        if (!e.is_object() || !e.contains("count") || !e["count"].is_number_integer()) {
            continue;
        }
        const std::string d = e.value("difficulty", "");
        const int n = e["count"].get<int>();
        if (d == "All") {
            s.total = n;
            seen = true;
        } else if (d == "Easy") {
            s.easy = n;
        } else if (d == "Medium") {
            s.medium = n;
        } else if (d == "Hard") {
            s.hard = n;
        }
    }
    if (!seen) {
        return std::nullopt;
    }
    return s;
}

std::optional<Stats> fetch(const std::string& username, std::string& why) {
    try {
        const json body{{"query", kQuery}, {"variables", {{"u", username}}}};
        const auto r = Net::Http::transport().post_json(
            kGraphqlUrl,
            body.dump(),
            {{"Referer", "https://leetcode.com/u/" + username + "/"}, {"User-Agent", "Mozilla/5.0 (Life OS)"}},
            15);
        if (r.status != 200) {
            why = "LeetCode answered " + std::to_string(r.status);
            return std::nullopt;
        }
        auto s = parse_stats(r.body);
        if (!s) {
            why = "no such public profile";
        }
        return s;
    } catch (const std::exception& e) {
        why = std::string("LeetCode is unreachable: ") + e.what();
        return std::nullopt;
    }
}

json process_sync(const json& /*payload*/) {
    Repositories::LeetCode::ProfileRepository repo;
    int ok = 0;
    int failed = 0;
    for (const auto& p : repo.all()) {
        std::string why;
        if (const auto s = fetch(p.username, why)) {
            repo.snapshot(p.owner, p.tz, s->total, s->easy, s->medium, s->hard);
            ++ok;
        } else {
            spdlog::warn("leetcode: pull of a profile failed: {}", why);
            repo.failed(p.owner, why);
            ++failed;
        }
    }
    return json{{"status", "done"}, {"ok", ok}, {"failed", failed}};
}

}  // namespace LeetCode
