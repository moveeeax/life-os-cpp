/**
 * @file ObjectStore.hpp
 * @brief The bucket of the wallpaper library as the API sees it: an upload
 *        URL for the browser, HEAD and DELETE for itself, and the public URL
 *        a stored object is read from. The configured store is an S3Client;
 *        API tests install a fake.
 */

#pragma once

#include <string>

namespace Storage {

/// Objects never change under their key (a new upload gets a new id), so the
/// browser may keep them a year without asking again. Signed into every upload.
inline constexpr const char* kImmutableCacheControl = "public, max-age=31536000, immutable";

struct ObjectInfo {
    bool exists = false;
    long long bytes = 0;
};

class ObjectStore {
public:
    virtual ~ObjectStore() = default;

    /// A presigned PUT valid 15 minutes, signed with the content type, length and kImmutableCacheControl.
    virtual std::string upload_url(const std::string& key, const std::string& content_type, long long bytes) const = 0;
    /// Throws std::runtime_error when the store does not answer.
    virtual ObjectInfo head(const std::string& key) const = 0;
    /// A missing object is fine; throws std::runtime_error when the store does not answer.
    virtual void remove(const std::string& key) const = 0;
    virtual std::string public_url(const std::string& key) const = 0;
    /// prefix + folder + "/" + id + "/" + name, e.g. folder "wallpapers" or "sounds".
    virtual std::string key_for(const std::string& folder, const std::string& id, const std::string& name) const = 0;
};

/// The configured store, or nullptr while storage.s3 is incomplete.
ObjectStore* object_store();

/// Tests swap the store; nullptr restores the configured one.
void install_object_store_for_testing(ObjectStore* store);

}  // namespace Storage
