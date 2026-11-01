/**
 * @file Sync.cpp
 * @brief Body for src/github/jobs/Sync.hpp.
 */

#include "github/jobs/Sync.hpp"

#include <chrono>
#include <ctime>
#include <exception>
#include <map>
#include <vector>

#include <spdlog/spdlog.h>

#include "cache/Cache.hpp"
#include "jobs/Jobs.hpp"
#include "net/Http.hpp"
#include "repositories/github/LinkRepository.hpp"
#include "repositories/github/MuteRepository.hpp"
#include "utils/Seal.hpp"

namespace Github {

namespace {

using json = nlohmann::json;
using Repositories::Github::LinkRepository;
using Repositories::Github::MuteRepository;
using Repositories::Github::RepoRepository;

/// The lock of one owner's sync, released when the sync is over. Without a cache every sync runs.
struct Lock {
    std::string key;
    bool held = false;
    explicit Lock(const std::string& owner) : key(lock_key(owner)) {
        if (!Cache::is_initialized()) {
            held = true;
            return;
        }
        held = Cache::get().set_nx(key, "1", std::chrono::seconds(kLockSeconds));
    }
    ~Lock() {
        if (held && Cache::is_initialized()) {
            try {
                Cache::get().del(key);
            } catch (const std::exception&) {}
        }
    }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
};

std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) {
        if (!out.empty()) {
            out += ", ";
        }
        out += s;
    }
    return out;
}

json failed(LinkRepository& links, const std::string& owner, const std::string& code, const std::string& why) {
    spdlog::warn("github: the sync of a user failed: {} ({})", code, why);
    links.mark_failed(owner, code);
    return json{{"status", "failed"}, {"error", code}};
}

}  // namespace

std::string lock_key(const std::string& owner) {
    return "github:sync:lock:" + owner;
}

RepoRepository::Input input_of(const Repo& r, const std::set<std::string>& muted, long long now) {
    RepoRepository::Input in;
    in.node_id = r.node_id;
    in.full_name = r.full_name;
    in.owner = r.owner;
    in.name = r.name;
    in.visibility = r.visibility;
    in.fork = r.fork;
    in.archived = r.archived;
    in.description = r.description;
    in.topics = r.topics;
    in.license = r.license;
    in.stars = r.stars;
    in.pushed_at = r.pushed_at;
    in.default_branch = r.default_branch;
    in.open_issues = r.open_issues;
    in.open_prs = r.open_prs;
    in.open_alerts = r.open_alerts;
    in.checks = r.checks;
    in.has_readme = r.has_readme;
    in.attention = attention(facts_of(r), muted, now);
    return in;
}

json sync_one(const std::string& owner) {
    LinkRepository links;
    const auto link = links.get(owner);
    if (!link) {
        return json{{"status", "skipped"}, {"reason", "not_linked"}};
    }
    Lock lock(owner);
    if (!lock.held) {
        return json{{"status", "skipped"}, {"reason", "running"}};
    }
    const auto s = settings();
    if (!s) {
        return failed(links, owner, "not_configured", "no OAuth App settings");
    }
    std::string token;
    try {
        token = Utils::Seal::unseal({link->sealed, link->nonce}, s->token_key);
    } catch (const Utils::Seal::SealError& e) {
        return failed(links, owner, "token_unreadable", e.what());
    }
    auto pulled = pull(Net::Http::transport(), token);
    if (auto* f = std::get_if<Failure>(&pulled)) {
        return failed(links, owner, f->code, f->why);
    }
    const Pull& p = std::get<Pull>(pulled);
    const auto mutes = MuteRepository().all(owner);
    const long long now = static_cast<long long>(std::time(nullptr));
    std::vector<RepoRepository::Input> rows;
    rows.reserve(p.repos.size());
    static const std::set<std::string> kNone;
    for (const auto& r : p.repos) {
        const auto m = mutes.find(r.full_name);
        rows.push_back(input_of(r, m == mutes.end() ? kNone : m->second, now));
    }
    RepoRepository().replace(owner, rows);
    links.mark_synced(owner, join(p.empty_orgs));
    spdlog::info("github: synced {} repositories of a user ({} organisations, {} empty)",
                 rows.size(),
                 p.orgs.size(),
                 p.empty_orgs.size());
    return json{{"status", "done"}, {"repos", rows.size()}, {"orgs", p.orgs}, {"empty_orgs", p.empty_orgs}};
}

json process_sync(const json& payload) {
    const auto owner = payload.is_object() ? payload.find("owner") : payload.end();
    if (owner != payload.end() && owner->is_string() && !owner->get<std::string>().empty()) {
        return sync_one(owner->get<std::string>());
    }
    // The tick: one job per user whose sync is due, so a slow user does not hold the others.
    int queued = 0;
    for (const auto& due : LinkRepository().due(kSyncEveryHours)) {
        if (!Jobs::is_initialized()) {
            break;
        }
        try {
            Jobs::get().submit(kSyncJob, json{{"owner", due}});
            ++queued;
        } catch (const std::exception& e) {
            spdlog::warn("github: could not queue the sync of a user: {}", e.what());
        }
    }
    return json{{"status", "queued"}, {"owners", queued}};
}

}  // namespace Github
