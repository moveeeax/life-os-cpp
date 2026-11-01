/**
 * @file Client.cpp
 * @brief Body for src/github/Client.hpp.
 */

#include "github/Client.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>

#include <spdlog/spdlog.h>

namespace Github {

namespace {

using json = nlohmann::json;

/// The fields of one repository, shared by the two queries below.
constexpr const char* kRepoFragment = R"(
fragment RepoFields on Repository {
  id
  name
  nameWithOwner
  owner { login }
  visibility
  isFork
  isArchived
  description
  repositoryTopics(first: 20) { nodes { topic { name } } }
  licenseInfo { spdxId }
  stargazerCount
  pushedAt
  defaultBranchRef { name target { ... on Commit { statusCheckRollup { state } } } }
  readmeMd: object(expression: "HEAD:README.md") { id }
  readmePlain: object(expression: "HEAD:README") { id }
  readmeRst: object(expression: "HEAD:README.rst") { id }
  issues(states: OPEN) { totalCount }
  pullRequests(states: OPEN) { totalCount }
  vulnerabilityAlerts(states: OPEN) { totalCount }
})";

/// The viewer, their organisations and one page of the repositories they own.
constexpr const char* kViewerQuery = R"(
query($after: String, $first: Int!) {
  viewer {
    login
    organizations(first: 100) { nodes { login } }
    repositories(first: $first, after: $after, affiliations: [OWNER], ownerAffiliations: [OWNER]) {
      pageInfo { hasNextPage endCursor }
      nodes { ...RepoFields }
    }
  }
})";

/// One page of an organisation's repositories.
constexpr const char* kOrgQuery = R"(
query($login: String!, $after: String, $first: Int!) {
  organization(login: $login) {
    repositories(first: $first, after: $after) {
      pageInfo { hasNextPage endCursor }
      nodes { ...RepoFields }
    }
  }
})";

constexpr const char* kLoginQuery = "query { viewer { login } }";

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

/// Standard base64 with padding (RFC 4648 section 4), for the Basic credentials of the revoke call.
std::string base64(const std::string& in) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    while (i + 2 < in.size()) {
        const unsigned n = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) |
                           static_cast<unsigned char>(in[i + 2]);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += kAlphabet[n & 63];
        i += 3;
    }
    if (i + 1 == in.size()) {
        const unsigned n = static_cast<unsigned char>(in[i]) << 16;
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += "==";
    } else if (i + 2 == in.size()) {
        const unsigned n = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> api_headers(const std::string& token) {
    return {{"Authorization", "Bearer " + token},
            {"Accept", "application/vnd.github+json"},
            {"X-GitHub-Api-Version", kApiVersion},
            {"User-Agent", kUserAgent}};
}

/// "repo,read:org" (the token answer) or "repo, read:org" (X-OAuth-Scopes) as "repo read:org".
std::string normalise_scopes(const std::string& raw) {
    std::string out;
    std::string word;
    const auto flush = [&] {
        if (!word.empty()) {
            if (!out.empty()) {
                out += ' ';
            }
            out += word;
            word.clear();
        }
    };
    for (const char c : raw) {
        if (c == ',' || c == ' ' || c == '\t') {
            flush();
        } else {
            word += c;
        }
    }
    flush();
    return out;
}

/// The Failure a non-2xx status names; nullopt for a 2xx.
std::optional<Failure> failure_of(const Net::Http::Response& r) {
    if (r.status >= 200 && r.status < 300) {
        return std::nullopt;
    }
    const std::string answered = "GitHub answered " + std::to_string(r.status);
    if (r.status == 401) {
        return Failure{"token_revoked", answered};
    }
    if (r.status == 429) {
        return Failure{"rate_limited", answered};
    }
    if (r.status == 403) {
        const std::string body = lower(r.body);
        if (body.find("rate limit") != std::string::npos || body.find("abuse") != std::string::npos) {
            return Failure{"rate_limited", answered + " (rate limit)"};
        }
        return Failure{"forbidden", answered};
    }
    if (r.status >= 500 || r.status == 0) {
        return Failure{"unavailable", answered};
    }
    return Failure{"bad_answer", answered};
}

std::string text(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

int count_of(const json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_object()) {
        return 0;
    }
    const auto n = it->find("totalCount");
    return n != it->end() && n->is_number_integer() ? std::max(0, n->get<int>()) : 0;
}

bool present(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_object();
}

/// Every page of a repositories connection, read by @p page(after) -> the connection object or nullopt.
template <class Page>
std::optional<Failure> paginate(std::vector<Repo>& into, Page&& page) {
    json after = nullptr;
    for (int n = 0; n < kMaxPages; ++n) {
        auto conn = page(after);
        if (auto* f = std::get_if<Failure>(&conn)) {
            return *f;
        }
        const json& c = std::get<json>(conn);
        if (!c.is_object()) {
            return std::nullopt;
        }
        const auto nodes = c.find("nodes");
        if (nodes != c.end() && nodes->is_array()) {
            for (const auto& node : *nodes) {
                if (auto r = repo_of(node)) {
                    into.push_back(std::move(*r));
                }
            }
        }
        const auto info = c.find("pageInfo");
        if (info == c.end() || !info->is_object() || !info->value("hasNextPage", false)) {
            return std::nullopt;
        }
        const std::string cursor = text(*info, "endCursor");
        if (cursor.empty()) {
            return std::nullopt;
        }
        after = cursor;
    }
    spdlog::warn("github: more than {} pages of repositories; the rest is left for the next sync", kMaxPages);
    return std::nullopt;
}

}  // namespace

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
    return std::string(kAuthorizeUrl) + "?client_id=" + encode(s.client_id) +
           "&redirect_uri=" + encode(s.redirect_url) + "&scope=" + encode(kScope) + "&state=" + encode(state);
}

Tokens exchange(Net::Http::Transport& t, const Settings& s, const std::string& code) {
    Tokens out;
    Net::Http::Request r;
    r.method = "POST";
    r.url = kTokenUrl;
    r.body = "client_id=" + encode(s.client_id) + "&client_secret=" + encode(s.client_secret) +
             "&code=" + encode(code) + "&redirect_uri=" + encode(s.redirect_url);
    r.content_type = "application/x-www-form-urlencoded";
    r.headers = {{"Accept", "application/json"}, {"User-Agent", kUserAgent}};
    r.timeout_seconds = 15;
    Net::Http::Response res;
    try {
        res = t.send(r);
    } catch (const Net::Http::TransportError&) {
        out.error = "unavailable";
        return out;
    }
    // GitHub answers 200 with {error, error_description} for a bad or used code.
    const json j = json::parse(res.body, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object()) {
        out.error = res.status >= 200 && res.status < 300 ? "bad_answer" : "http_" + std::to_string(res.status);
        return out;
    }
    if (!text(j, "error").empty()) {
        out.error = text(j, "error");
        return out;
    }
    if (res.status < 200 || res.status >= 300) {
        out.error = "http_" + std::to_string(res.status);
        return out;
    }
    out.access_token = text(j, "access_token");
    out.scopes = normalise_scopes(text(j, "scope"));
    if (out.access_token.empty()) {
        out.error = "no_access_token";
    }
    return out;
}

bool revoke(Net::Http::Transport& t, const Settings& s, const std::string& token) {
    Net::Http::Request r;
    r.method = "DELETE";
    r.url = std::string(kApiUrl) + "/applications/" + encode(s.client_id) + "/token";
    r.body = json{{"access_token", token}}.dump();
    r.content_type = "application/json";
    r.headers = {{"Authorization", "Basic " + base64(s.client_id + ":" + s.client_secret)},
                 {"Accept", "application/vnd.github+json"},
                 {"X-GitHub-Api-Version", kApiVersion},
                 {"User-Agent", kUserAgent}};
    r.timeout_seconds = 10;
    try {
        const auto res = t.send(r);
        return res.status >= 200 && res.status < 300;
    } catch (const Net::Http::TransportError&) {
        return false;
    }
}

std::variant<json, Failure> graphql(Net::Http::Transport& t,
                                    const std::string& token,
                                    const std::string& query,
                                    const json& variables) {
    Net::Http::Response res;
    try {
        res = t.post_json(kGraphqlUrl, json{{"query", query}, {"variables", variables}}.dump(), api_headers(token), 30);
    } catch (const Net::Http::TransportError& e) {
        return Failure{"unavailable", std::string("GitHub is unreachable: ") + e.what()};
    }
    if (auto f = failure_of(res)) {
        return *f;
    }
    const json j = json::parse(res.body, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object()) {
        return Failure{"bad_answer", "GitHub answered something that is not JSON"};
    }
    std::string first_error;
    const auto errors = j.find("errors");
    if (errors != j.end() && errors->is_array()) {
        for (const auto& e : *errors) {
            if (!e.is_object()) {
                continue;
            }
            const std::string type = text(e, "type");
            if (type == "RATE_LIMITED") {
                return Failure{"rate_limited", "GitHub rate-limited the query: " + text(e, "message")};
            }
            if (first_error.empty()) {
                first_error = type.empty() ? text(e, "message") : type + ": " + text(e, "message");
            }
        }
    }
    const auto data = j.find("data");
    if (data == j.end() || !data->is_object()) {
        // No data at all: the whole query was refused (an org the app may not see is a null inside data,
        // handled by the caller; this is the query itself).
        if (first_error.rfind("FORBIDDEN", 0) == 0 || first_error.rfind("INSUFFICIENT_SCOPES", 0) == 0) {
            return Failure{"forbidden", first_error};
        }
        return Failure{"bad_answer", first_error.empty() ? "no data in the answer" : first_error};
    }
    if (!first_error.empty()) {
        spdlog::debug("github: partial answer: {}", first_error);
    }
    return *data;
}

std::variant<std::string, Failure> viewer_login(Net::Http::Transport& t, const std::string& token) {
    auto out = graphql(t, token, kLoginQuery, json::object());
    if (auto* f = std::get_if<Failure>(&out)) {
        return *f;
    }
    const json& data = std::get<json>(out);
    const auto viewer = data.find("viewer");
    if (viewer == data.end() || !viewer->is_object() || text(*viewer, "login").empty()) {
        return Failure{"bad_answer", "no viewer in the answer"};
    }
    return text(*viewer, "login");
}

std::string checks_of(const std::string& state) {
    if (state == "SUCCESS") {
        return "success";
    }
    if (state == "FAILURE" || state == "ERROR") {
        return "failure";
    }
    if (state == "PENDING" || state == "EXPECTED") {
        return "pending";
    }
    return "none";
}

std::optional<Repo> repo_of(const json& node) {
    if (!node.is_object()) {
        return std::nullopt;
    }
    Repo r;
    r.node_id = text(node, "id");
    r.full_name = text(node, "nameWithOwner");
    r.name = text(node, "name");
    if (r.node_id.empty() || r.name.empty()) {
        return std::nullopt;
    }
    const auto owner = node.find("owner");
    if (owner != node.end() && owner->is_object()) {
        r.owner = text(*owner, "login");
    }
    if (r.full_name.empty() && !r.owner.empty()) {
        r.full_name = r.owner + "/" + r.name;
    }
    if (r.owner.empty()) {
        const auto slash = r.full_name.find('/');
        r.owner = slash == std::string::npos ? std::string() : r.full_name.substr(0, slash);
    }
    if (r.full_name.empty() || r.owner.empty()) {
        return std::nullopt;
    }
    const std::string visibility = lower(text(node, "visibility"));
    r.visibility = visibility == "public" || visibility == "internal" ? visibility : "private";
    r.fork = node.value("isFork", false);
    r.archived = node.value("isArchived", false);
    r.description = text(node, "description");
    const auto topics = node.find("repositoryTopics");
    if (topics != node.end() && topics->is_object()) {
        const auto nodes = topics->find("nodes");
        if (nodes != topics->end() && nodes->is_array()) {
            for (const auto& t : *nodes) {
                if (!t.is_object()) {
                    continue;
                }
                const auto topic = t.find("topic");
                if (topic != t.end() && topic->is_object() && !text(*topic, "name").empty()) {
                    r.topics.push_back(text(*topic, "name"));
                }
            }
        }
    }
    const auto license = node.find("licenseInfo");
    if (license != node.end() && license->is_object()) {
        r.license = text(*license, "spdxId");
    }
    const auto stars = node.find("stargazerCount");
    if (stars != node.end() && stars->is_number_integer()) {
        r.stars = std::max(0, stars->get<int>());
    }
    if (!text(node, "pushedAt").empty()) {
        r.pushed_at = text(node, "pushedAt");
    }
    const auto branch = node.find("defaultBranchRef");
    if (branch != node.end() && branch->is_object()) {
        r.default_branch = text(*branch, "name");
        const auto target = branch->find("target");
        if (target != branch->end() && target->is_object()) {
            const auto rollup = target->find("statusCheckRollup");
            if (rollup != target->end() && rollup->is_object()) {
                r.checks = checks_of(text(*rollup, "state"));
            }
        }
    }
    r.has_readme = present(node, "readmeMd") || present(node, "readmePlain") || present(node, "readmeRst");
    r.open_issues = count_of(node, "issues");
    r.open_prs = count_of(node, "pullRequests");
    r.open_alerts = count_of(node, "vulnerabilityAlerts");
    return r;
}

RepoFacts facts_of(const Repo& r) {
    RepoFacts f;
    f.full_name = r.full_name;
    f.is_public = r.visibility == "public";
    f.fork = r.fork;
    f.archived = r.archived;
    f.description = r.description;
    f.license = r.license;
    f.has_readme = r.has_readme;
    f.open_issues = r.open_issues;
    f.open_prs = r.open_prs;
    f.open_alerts = r.open_alerts;
    f.checks = r.checks;
    f.default_branch = r.default_branch;
    if (r.pushed_at) {
        f.pushed_at = parse_time(*r.pushed_at);
    }
    return f;
}

std::optional<long long> parse_time(const std::string& s) {
    // YYYY-MM-DDTHH:MM:SS, then Z or an offset; GitHub writes Z.
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
    if (s.size() < 20 || !digits(0, 4, y) || s[4] != '-' || !digits(5, 2, mo) || s[7] != '-' || !digits(8, 2, d) ||
        s[10] != 'T' || !digits(11, 2, h) || s[13] != ':' || !digits(14, 2, mi) || s[16] != ':' || !digits(17, 2, se) ||
        mo < 1 || mo > 12 || d < 1 || d > 31) {
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
    const unsigned doy = (153 * (static_cast<unsigned>(mo) + (mo > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long long days = era * 146097 + static_cast<long long>(doe) - 719468;
    return days * 86400 + h * 3600LL + mi * 60LL + se - offset;
}

std::variant<Pull, Failure> pull(Net::Http::Transport& t, const std::string& token) {
    Pull out;
    const std::string viewer_query = std::string(kViewerQuery) + kRepoFragment;
    const std::string org_query = std::string(kOrgQuery) + kRepoFragment;
    bool first = true;
    auto failed = paginate(out.repos, [&](const json& after) -> std::variant<json, Failure> {
        auto data = graphql(t, token, viewer_query, json{{"after", after}, {"first", kPageSize}});
        if (auto* f = std::get_if<Failure>(&data)) {
            return *f;
        }
        const json& d = std::get<json>(data);
        const auto viewer = d.find("viewer");
        if (viewer == d.end() || !viewer->is_object()) {
            return Failure{"bad_answer", "no viewer in the answer"};
        }
        if (first) {
            first = false;
            out.login = text(*viewer, "login");
            const auto orgs = viewer->find("organizations");
            if (orgs != viewer->end() && orgs->is_object()) {
                const auto nodes = orgs->find("nodes");
                if (nodes != orgs->end() && nodes->is_array()) {
                    for (const auto& o : *nodes) {
                        if (o.is_object() && !text(o, "login").empty()) {
                            out.orgs.push_back(text(o, "login"));
                        }
                    }
                }
            }
        }
        return viewer->value("repositories", json::object());
    });
    if (failed) {
        return *failed;
    }
    for (const auto& org : out.orgs) {
        const std::size_t before = out.repos.size();
        bool reachable = true;
        failed = paginate(out.repos, [&](const json& after) -> std::variant<json, Failure> {
            auto data = graphql(t, token, org_query, json{{"login", org}, {"after", after}, {"first", kPageSize}});
            if (auto* f = std::get_if<Failure>(&data)) {
                // A refusal of one organisation is that organisation's hint, not the whole sync's failure.
                if (f->code == "forbidden") {
                    reachable = false;
                    return json(nullptr);
                }
                return *f;
            }
            const json& d = std::get<json>(data);
            const auto o = d.find("organization");
            if (o == d.end() || !o->is_object()) {
                reachable = false;
                return json(nullptr);
            }
            return o->value("repositories", json::object());
        });
        if (failed) {
            return *failed;
        }
        if (!reachable || out.repos.size() == before) {
            out.empty_orgs.push_back(org);
        }
    }
    return out;
}

}  // namespace Github
