/**
 * @file Kube.hpp
 * @brief The few Kubernetes API calls the agent needs: create, read, list and
 *        delete pods and secrets in one namespace. From inside the cluster:
 *        the service account token and CA the kubelet mounts into the worker
 *        (the API pod has none). The transport is injected, so tests script
 *        the API server.
 */

#pragma once

#include <optional>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "net/Http.hpp"

namespace Agent::Kube {

/// The API server refused or did not answer; status 0 when no answer came.
struct Error : std::runtime_error {
    Error(long s, const std::string& what) : std::runtime_error(what), status(s) {}
    long status;
};

inline constexpr const char* kServiceAccountDir = "/var/run/secrets/kubernetes.io/serviceaccount";
inline constexpr const char* kInClusterUrl = "https://kubernetes.default.svc";

class Client {
public:
    /// @p kind is the plural resource of the core API group: "pods" or "secrets".
    Client(Net::Http::Transport& transport, std::string api_url, std::string token, std::string ca_file);

    /// The worker's own credentials; nullopt when no service account is mounted.
    static std::optional<Client> in_cluster(Net::Http::Transport& transport);

    /// Creates the object; false when one of that name already exists (409).
    bool create(const std::string& ns, const std::string& kind, const nlohmann::json& object);
    /// The object, or nullopt when there is none (404).
    std::optional<nlohmann::json> get(const std::string& ns, const std::string& kind, const std::string& name);
    /// The objects carrying @p label_selector (e.g. "a=b"): the `items` of the list.
    nlohmann::json list(const std::string& ns, const std::string& kind, const std::string& label_selector);
    /// Deletes the object; a missing one is fine. @p grace_seconds < 0 keeps the object's own grace period.
    void remove(const std::string& ns, const std::string& kind, const std::string& name, int grace_seconds = -1);

private:
    Net::Http::Response call(const std::string& method, const std::string& path, const std::string& body);
    std::string path_of(const std::string& ns, const std::string& kind, const std::string& name = "") const;

    Net::Http::Transport& transport_;
    std::string api_url_;
    std::string token_;
    std::string ca_file_;
};

}  // namespace Agent::Kube
