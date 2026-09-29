/**
 * @file Storage.hpp
 * @brief Object/file storage seam. A small backend interface plus a local-disk
 *        implementation, behind a global accessor (mirrors Cache/Email).
 *
 * The interface is the point: a fork swaps LocalStorage for an S3/GCS backend by
 * subclassing StorageBackend and installing it — call sites (an upload
 * controller, a job) don't change. Keys are opaque strings the caller chooses
 * (use a UUID, not a user-supplied filename); LocalStorage refuses traversal.
 *
 * Wiring an HTTP upload/download surface (multipart parse, a files metadata
 * table, owner-scoping) is app-specific — see docs/EXAMPLES or mirror the
 * api_keys controller. This layer is just durable get/put/remove.
 *
 * Non-template bodies (both backends, the SigV4 signing, the global-instance
 * lifecycle) live in Storage.cpp (compiled once into app_core; ADR 0003 as
 * amended 2026-08-22).
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "utils/Config.hpp"

namespace Storage {

/// Backend contract. All methods throw std::runtime_error on an unexpected I/O
/// failure; get()/exists() return empty/false for a simply-absent object.
class StorageBackend {
public:
    /// One stored object, as reported by list().
    struct ObjectInfo {
        std::string key;
        std::size_t size_bytes = 0;
        std::string last_modified;  // ISO 8601 UTC, or empty when unknown
    };

    virtual ~StorageBackend() = default;
    virtual void put(const std::string& key, const std::string& bytes, const std::string& content_type) = 0;
    virtual std::optional<std::string> get(const std::string& key) = 0;
    virtual bool remove(const std::string& key) = 0;
    virtual bool exists(const std::string& key) = 0;
    /// Objects under @p prefix, newest first. Backends may truncate very large
    /// listings (S3: one ListObjectsV2 page, 1000 keys) — they log when so.
    virtual std::vector<ObjectInfo> list(const std::string& prefix) = 0;
    /// A URL/locator a client can use to fetch the object (backend-specific:
    /// a public base + key for local/CDN, or a presigned URL for S3).
    virtual std::string url(const std::string& key) = 0;
};

/// Same-origin path prefix under which the backend serves LocalStorage objects
/// when no CDN/public base URL is configured — see the `GET /uploads/{key}`
/// route and nginx's `/uploads/` proxy. url() returns `<prefix><key>`, which is
/// absolute, so a stored `![](…)` still resolves from /posts/<slug> or
/// /admin/media (a bare key did not).
inline constexpr const char* kLocalPublicPrefix = "/uploads/";

/// Reject keys that could escape the storage root (path traversal / absolute
/// paths). Keys are meant to be opaque ids; this is defense-in-depth.
bool key_is_safe(const std::string& key);

/// Local-filesystem backend. Stores each object as a file under `root`.
class LocalStorage : public StorageBackend {
public:
    LocalStorage(std::filesystem::path root, std::string public_base)
        : root_(std::move(root)), public_base_(std::move(public_base)) {}

    void put(const std::string& key, const std::string& bytes, const std::string& content_type) override;

    std::optional<std::string> get(const std::string& key) override;

    bool remove(const std::string& key) override;

    bool exists(const std::string& key) override;

    std::vector<ObjectInfo> list(const std::string& prefix) override;

    /// With a public base (CDN/S3 gateway) → `<base>/<key>`. Without one →
    /// the same-origin `/uploads/<key>` path the backend serves this root on;
    /// returning the bare key made every stored image resolve relative to the
    /// page that embedded it, and 404.
    std::string url(const std::string& key) override;

private:
    std::filesystem::path resolve(const std::string& key) const;

    std::filesystem::path root_;
    std::string public_base_;
};

/// S3-compatible backend (MinIO, AWS S3, Cloudflare R2, …) over libcurl with
/// hand-rolled AWS Signature V4. Path-style addressing by default (what MinIO
/// and most self-hosted gateways want). Unlike LocalStorage this survives pod
/// restarts and is shared across replicas — the right choice for k8s.
class S3Storage : public StorageBackend {
public:
    struct Config {
        std::string endpoint;  // e.g. http://minio:9000 (scheme required)
        std::string region;    // e.g. us-east-1 (MinIO default)
        std::string bucket;
        std::string access_key;
        std::string secret_key;
        std::string public_base;  // public URL prefix for url(); empty → endpoint/bucket
        // Whole-request and TCP-connect budgets. Every S3 call runs inline on a
        // Drogon IO thread, so these bound how long one blackholed object store
        // can pin 1 of N event loops — keep them well under the readiness probe.
        long timeout_sec = 10;
        long connect_timeout_sec = 2;
    };

    explicit S3Storage(Config cfg);

    void put(const std::string& key, const std::string& bytes, const std::string& content_type) override;

    std::optional<std::string> get(const std::string& key) override;

    bool remove(const std::string& key) override;

    bool exists(const std::string& key) override;

    /// Parse a ListObjectsV2 XML body into ObjectInfo rows (newest first).
    /// Static + pure so tests can feed canned MinIO/AWS responses without a
    /// network. Sets @p truncated when S3 reports more than one page.
    static std::vector<ObjectInfo> parse_list_objects_xml(const std::string& body, bool* truncated = nullptr);

    std::vector<ObjectInfo> list(const std::string& prefix) override;

    std::string url(const std::string& key) override;

private:
    // Read callback feeding the request body to libcurl during a PUT upload.
    struct ReadCtx {
        const std::string* data;
        std::size_t offset = 0;
    };
    static std::size_t read_cb(char* buffer, std::size_t size, std::size_t nitems, void* userdata);
    static std::size_t write_cb(char* ptr, std::size_t size, std::size_t nmemb, void* userdata);

    static std::string host_from_endpoint(const std::string& ep);

    // RFC 3986 percent-encoding, leaving unreserved chars. keep_slash leaves
    // '/' intact (path segment separators); query values encode it too (SigV4
    // canonical query values must).
    static std::string uri_encode(const std::string& s, bool keep_slash);

    static std::string uri_encode_path(const std::string& s);

    static std::string uri_encode_query(const std::string& s);

    static void amz_dates(std::string& amzdate, std::string& datestamp);

    long request(const std::string& method,
                 const std::string& key,
                 const std::string& body,
                 const std::string& content_type,
                 std::string* out);

    // SigV4-signed request against an explicit canonical URI + query (query
    // must already be canonically encoded and '&'-joined in sorted key order).
    long signed_request(const std::string& method,
                        const std::string& canonical_uri,
                        const std::string& canonical_query,
                        const std::string& body,
                        const std::string& content_type,
                        std::string* out);

    Config cfg_;
    std::string host_;
};

// ── Global accessor (mirrors Cache/Email) ────────────────────────────────────
bool is_initialized();

StorageBackend& get();

/// Bring up the configured backend: "local" (default) or "s3" (MinIO/AWS/R2/…).
void initialize(Config::AppConfig& cfg);

// Test seam — swap in a fake/temp backend.
void install_for_testing(std::unique_ptr<StorageBackend> backend);
void reset_for_testing();

}  // namespace Storage
