/**
 * @file Lifecycle.cpp
 * @brief Bodies for src/agent/Lifecycle.hpp.
 */

#include "agent/Lifecycle.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <optional>
#include <set>
#include <string>

#include <spdlog/spdlog.h>

#include "agent/Kube.hpp"
#include "agent/Manifest.hpp"
#include "agent/Settings.hpp"
#include "cache/Lock.hpp"
#include "jobs/Job.hpp"
#include "jobs/Jobs.hpp"
#include "net/Http.hpp"
#include "repositories/agent/AgentRepository.hpp"
#include "storage/ObjectStore.hpp"
#include "utils/Crypto.hpp"

namespace Agent {

namespace {

using json = nlohmann::json;
using Repositories::Agent::AgentRepository;

/// SigV4 presigned URLs live at most 7 days.
constexpr long kMaxPresignSeconds = 7L * 24 * 3600;

/// One job per user at a time: start, stop and reconcile never race on a pod.
/// Released only by its holder (Cache::Lock), so a job that outlived the TTL
/// does not free the lock of the next one.
class OwnerLock : public Cache::Lock {
public:
    explicit OwnerLock(const std::string& owner) : Cache::Lock("ani:agent:lock:" + owner, std::chrono::seconds(120)) {}
};

std::string owner_of(const json& payload) {
    const auto owner = payload.value("owner", std::string());
    if (owner.empty()) {
        throw Jobs::PermanentJobError("ani agent job without an owner");
    }
    return owner;
}

void submit(const char* type, const std::string& owner) {
    Jobs::get().submit(type, json{{"owner", owner}});
}

std::string snapshot_key(const Storage::ObjectStore& store, const std::string& owner, int generation) {
    return store.private_key(owner, "ani", Manifest::snapshot_name(generation));
}

/// Stops a run that went wrong: the row records why, the stop job cleans up.
json fail(const std::string& owner, const std::string& why) {
    spdlog::warn("ani agent {}: {}", Manifest::owner_hash(owner), why);
    AgentRepository repo;
    if (repo.begin_stop(owner, why)) {
        submit(kStopJob, owner);
    }
    return json{{"status", "failed"}, {"reason", why}};
}

std::optional<Kube::Client> kube() {
    return Kube::Client::in_cluster(Net::Http::transport());
}

bool pod_ready(const json& pod) {
    for (const auto& c : pod.value(json::json_pointer("/status/conditions"), json::array())) {
        if (c.value("type", "") == "Ready" && c.value("status", "") == "True") {
            return true;
        }
    }
    return false;
}

std::string pod_phase(const json& pod) {
    return pod.value(json::json_pointer("/status/phase"), std::string());
}

bool pod_ended(const json& pod) {
    const auto phase = pod_phase(pod);
    return phase == "Failed" || phase == "Succeeded";
}

long long seconds(const json& row, const char* field) {
    return row.contains(field) && row[field].is_number_integer() ? row[field].get<long long>() : 0;
}

}  // namespace

Wake wake(const std::string& owner) {
    const auto s = read_settings();
    if (!s) {
        return Wake::Off;
    }
    AgentRepository repo;
    if (const auto row = repo.get(owner); row && row->value("state", "stopped") != "stopped") {
        repo.touch(owner);
        return Wake::AlreadyUp;
    }
    if (repo.count_active() >= s->max_running) {
        return Wake::Busy;
    }
    if (!repo.begin_start(owner)) {
        repo.touch(owner);
        return Wake::AlreadyUp;
    }
    submit(kStartJob, owner);
    return Wake::Started;
}

void request_stop(const std::string& owner, const std::string& error) {
    try {
        if (!enabled() || !Jobs::is_initialized()) {
            return;
        }
        AgentRepository repo;
        if (repo.begin_stop(owner, error)) {
            submit(kStopJob, owner);
        }
    } catch (const std::exception& e) {
        spdlog::warn("ani agent: the stop of {} was not queued: {}", Manifest::owner_hash(owner), e.what());
    }
}

json process_start(const json& payload) {
    const std::string owner = owner_of(payload);
    OwnerLock lock(owner);
    if (!lock.held()) {
        throw Jobs::DeferJob("another job holds the agent", 2000);
    }
    AgentRepository repo;
    const auto row = repo.get(owner);
    if (!row || row->value("state", "") != "starting") {
        return json{{"status", "skipped"}, {"reason", "not_starting"}};
    }
    const auto s = read_settings();
    if (!s) {
        return fail(owner, "the agent is not configured");
    }
    auto client = kube();
    if (!client) {
        return fail(owner, "the worker has no Kubernetes service account");
    }
    const std::string name = Manifest::pod_name(owner);

    if ((*row)["pod_name"].is_null()) {
        // A pod of an earlier run still going away would save over the next snapshot.
        if (client->get(s->ns, "pods", name)) {
            throw Jobs::DeferJob("the previous pod is still there", 5000);
        }
        const Storage::ObjectStore* store = Storage::object_store();
        if (store == nullptr) {
            return fail(owner, "storage is not configured");
        }
        const int generation = row->value("generation", 0);
        Manifest::Run run;
        run.owner = owner;
        run.gateway_token = Utils::Crypto::random_hex(32);
        run.proxy_token = Utils::Crypto::random_hex(32);
        if (generation > 0) {
            run.get_url = store->download_url(snapshot_key(*store, owner, generation), 3600, "attachment");
        }
        const long put_seconds = std::min(kMaxPresignSeconds, static_cast<long>(s->max_hours + 1) * 3600);
        run.put_url = store->private_put_url(snapshot_key(*store, owner, generation + 1), put_seconds);
        if (!repo.set_run(owner,
                          name,
                          run.gateway_token,
                          Utils::Crypto::sha256_hex(run.proxy_token),
                          store->private_key(owner, "ani", ""))) {
            return json{{"status", "skipped"}, {"reason", "not_starting"}};
        }
        client->remove(s->ns, "secrets", name);
        client->create(s->ns, "secrets", Manifest::secret(*s, run));
        if (!client->create(s->ns, "pods", Manifest::pod(*s, run))) {
            return fail(owner, "a pod of that name appeared meanwhile");
        }
        spdlog::info("ani agent {}: pod created (snapshot {})", Manifest::owner_hash(owner), generation);
        throw Jobs::DeferJob("waiting for the pod", 3000);
    }

    const auto pod = client->get(s->ns, "pods", name);
    if (!pod) {
        return fail(owner, "the pod disappeared while starting");
    }
    if (pod_ended(*pod)) {
        return fail(owner, "the pod ended while starting (" + pod_phase(*pod) + ")");
    }
    if (pod_ready(*pod)) {
        const std::string ip = pod->value(json::json_pointer("/status/podIP"), std::string());
        repo.set_running(owner, ip);
        spdlog::info("ani agent {}: running after {} s", Manifest::owner_hash(owner), seconds(*row, "running_seconds"));
        return json{{"status", "running"}};
    }
    if (seconds(*row, "running_seconds") > kStartLimitSeconds) {
        return fail(owner, "the pod was not ready in " + std::to_string(kStartLimitSeconds) + " s");
    }
    throw Jobs::DeferJob("waiting for the pod", 3000);
}

json process_stop(const json& payload) {
    const std::string owner = owner_of(payload);
    OwnerLock lock(owner);
    if (!lock.held()) {
        throw Jobs::DeferJob("another job holds the agent", 2000);
    }
    AgentRepository repo;
    const auto row = repo.get(owner);
    if (!row || row->value("state", "") != "stopping") {
        return json{{"status", "skipped"}, {"reason", "not_stopping"}};
    }
    const int generation = row->value("generation", 0);
    const auto s = read_settings();
    auto client = kube();
    if (!s || !client || (*row)["pod_name"].is_null()) {
        // Nothing was created (or nothing can be reached): there is no pod to wait for.
        repo.set_stopped(owner, generation);
        return json{{"status", "stopped"}, {"generation", generation}};
    }
    const std::string name = (*row)["pod_name"].get<std::string>();
    if (const auto pod = client->get(s->ns, "pods", name)) {
        const bool deleting = pod->contains("metadata") && (*pod)["metadata"].contains("deletionTimestamp");
        if (!deleting) {
            client->remove(s->ns, "pods", name);
        } else if (seconds(*row, "state_seconds") > kStopForceSeconds) {
            client->remove(s->ns, "pods", name, 0);
        }
        throw Jobs::DeferJob("waiting for the pod to go", 3000);
    }

    int current = generation;
    if (const Storage::ObjectStore* store = Storage::object_store()) {
        const std::string next = snapshot_key(*store, owner, generation + 1);
        if (store->head(next).exists) {
            current = generation + 1;
            if (generation - 1 >= 1) {
                store->remove(snapshot_key(*store, owner, generation - 1));
            }
        } else {
            spdlog::warn("ani agent {}: the run saved no snapshot; snapshot {} stays current",
                         Manifest::owner_hash(owner),
                         generation);
        }
    }
    client->remove(s->ns, "secrets", name);
    repo.set_stopped(owner, current);
    spdlog::info("ani agent {}: stopped, snapshot {}", Manifest::owner_hash(owner), current);
    return json{{"status", "stopped"}, {"generation", current}};
}

json process_reconcile(const json& /*payload*/) {
    const auto s = read_settings();
    if (!s) {
        return json{{"status", "skipped"}, {"reason", "agent_off"}};
    }
    auto client = kube();
    if (!client) {
        return json{{"status", "skipped"}, {"reason", "no_service_account"}};
    }
    AgentRepository repo;
    const json rows = repo.active();
    std::set<std::string> owned;
    int stopped = 0;
    for (const auto& row : rows) {
        const std::string owner = row.value("owner_id", "");
        const std::string state = row.value("state", "");
        if (row["pod_name"].is_string()) {
            owned.insert(row["pod_name"].get<std::string>());
        }
        if (state == "starting" && seconds(row, "state_seconds") > kStartLimitSeconds) {
            request_stop(owner, "the start did not finish");
            ++stopped;
        } else if (state == "running") {
            std::string why;
            if (seconds(row, "idle_seconds") > static_cast<long long>(s->idle_minutes) * 60) {
                why = "idle";
            } else if (seconds(row, "running_seconds") > static_cast<long long>(s->max_hours) * 3600) {
                why = "max_hours";
            } else {
                const auto pod = client->get(s->ns, "pods", row.value("pod_name", ""));
                if (!pod || pod_ended(*pod)) {
                    why = "the pod ended";
                }
            }
            if (!why.empty()) {
                // Idle and age are normal stops: last_error stays as it was.
                request_stop(owner, why == "idle" || why == "max_hours" ? "" : why);
                spdlog::info("ani agent {}: stopping ({})", Manifest::owner_hash(owner), why);
                ++stopped;
            }
        } else if (state == "stopping" && seconds(row, "state_seconds") > kStopRetrySeconds) {
            submit(kStopJob, owner);
        }
    }

    // Pods no row owns: a run whose row moved on without them. Their keeper may
    // save a snapshot nobody reads; the next run of that user writes a newer one.
    int orphans = 0;
    const std::string selector = std::string(Manifest::kManagedLabel) + "=" + Manifest::kManagedBy;
    for (const auto& pod : client->list(s->ns, "pods", selector)) {
        const std::string name = pod.value(json::json_pointer("/metadata/name"), std::string());
        if (name.empty() || owned.count(name) != 0 || pod.contains(json::json_pointer("/metadata/deletionTimestamp"))) {
            continue;
        }
        client->remove(s->ns, "pods", name);
        client->remove(s->ns, "secrets", name);
        spdlog::warn("ani agent: deleted pod {}, which no agent owns", name);
        ++orphans;
    }
    return json{{"status", "done"}, {"active", rows.size()}, {"stopping", stopped}, {"orphans", orphans}};
}

}  // namespace Agent
