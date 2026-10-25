/**
 * @file Kube.cpp
 * @brief Bodies for src/agent/Kube.hpp.
 */

#include "agent/Kube.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include "storage/S3Presign.hpp"

namespace Agent::Kube {

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    std::stringstream out;
    out << in.rdbuf();
    std::string s = out.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) {
        s.pop_back();
    }
    return s;
}

/// The API server's message of a refusal, or the status alone.
std::string reason_of(const Net::Http::Response& r) {
    const auto j = nlohmann::json::parse(r.body, nullptr, /*allow_exceptions=*/false);
    if (j.is_object() && j.value("message", nlohmann::json()).is_string()) {
        return "status " + std::to_string(r.status) + ": " + j["message"].get<std::string>().substr(0, 300);
    }
    return "status " + std::to_string(r.status);
}

}  // namespace

Client::Client(Net::Http::Transport& transport, std::string api_url, std::string token, std::string ca_file)
    : transport_(transport), api_url_(std::move(api_url)), token_(std::move(token)), ca_file_(std::move(ca_file)) {}

std::optional<Client> Client::in_cluster(Net::Http::Transport& transport) {
    const std::string dir = kServiceAccountDir;
    if (!std::filesystem::exists(dir + "/token")) {
        return std::nullopt;
    }
    // The projected token rotates; a client is built per job, so it is read fresh each time.
    return Client(transport, kInClusterUrl, read_file(dir + "/token"), dir + "/ca.crt");
}

std::string Client::path_of(const std::string& ns, const std::string& kind, const std::string& name) const {
    std::string p = "/api/v1/namespaces/" + ns + "/" + kind;
    if (!name.empty()) {
        p += "/" + name;
    }
    return p;
}

Net::Http::Response Client::call(const std::string& method, const std::string& path, const std::string& body) {
    Net::Http::Request r;
    r.method = method;
    r.url = api_url_ + path;
    r.body = body;
    r.content_type = body.empty() ? "" : "application/json";
    r.headers = {{"Authorization", "Bearer " + token_}, {"Accept", "application/json"}};
    r.timeout_seconds = 15;
    r.ca_file = ca_file_;
    try {
        return transport_.send(r);
    } catch (const Net::Http::TransportError& e) {
        throw Error(0, std::string("no answer: ") + e.what());
    }
}

bool Client::create(const std::string& ns, const std::string& kind, const nlohmann::json& object) {
    const auto r = call("POST", path_of(ns, kind), object.dump());
    if (r.status == 201 || r.status == 200 || r.status == 202) {
        return true;
    }
    if (r.status == 409) {
        return false;
    }
    throw Error(r.status, "create " + kind + ": " + reason_of(r));
}

std::optional<nlohmann::json> Client::get(const std::string& ns, const std::string& kind, const std::string& name) {
    const auto r = call("GET", path_of(ns, kind, name), "");
    if (r.status == 404) {
        return std::nullopt;
    }
    if (r.status != 200) {
        throw Error(r.status, "get " + kind + "/" + name + ": " + reason_of(r));
    }
    auto j = nlohmann::json::parse(r.body, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object()) {
        throw Error(r.status, "get " + kind + "/" + name + ": not JSON");
    }
    return j;
}

nlohmann::json Client::list(const std::string& ns, const std::string& kind, const std::string& label_selector) {
    const auto r =
        call("GET", path_of(ns, kind) + "?labelSelector=" + Storage::S3::uri_encode(label_selector, false), "");
    if (r.status != 200) {
        throw Error(r.status, "list " + kind + ": " + reason_of(r));
    }
    const auto j = nlohmann::json::parse(r.body, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object() || !j.contains("items") || !j["items"].is_array()) {
        throw Error(r.status, "list " + kind + ": no items");
    }
    return j["items"];
}

void Client::remove(const std::string& ns, const std::string& kind, const std::string& name, int grace_seconds) {
    std::string body;
    if (grace_seconds >= 0) {
        body = nlohmann::json{{"kind", "DeleteOptions"}, {"apiVersion", "v1"}, {"gracePeriodSeconds", grace_seconds}}
                   .dump();
    }
    const auto r = call("DELETE", path_of(ns, kind, name), body);
    if (r.status == 200 || r.status == 202 || r.status == 404) {
        return;
    }
    throw Error(r.status, "delete " + kind + "/" + name + ": " + reason_of(r));
}

}  // namespace Agent::Kube
