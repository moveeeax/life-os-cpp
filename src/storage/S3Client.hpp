/**
 * @file S3Client.hpp
 * @brief The S3-compatible bucket behind the wallpaper library: every
 *        request, the API's own HEAD and DELETE included, goes to a
 *        presigned URL (S3Presign.hpp), so there is one signing path.
 */

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "storage/ObjectStore.hpp"
#include "storage/S3Config.hpp"

namespace Storage {

class S3Client : public ObjectStore {
public:
    explicit S3Client(S3::Settings settings);

    std::string upload_url(const std::string& key, const std::string& content_type, long long bytes) const override;
    ObjectInfo head(const std::string& key) const override;
    void remove(const std::string& key) const override;
    std::string public_url(const std::string& key) const override;
    std::string key_for(const std::string& folder, const std::string& id, const std::string& name) const override;

private:
    std::string signed_url(const std::string& method,
                           const std::string& key,
                           long expires_seconds,
                           std::vector<std::pair<std::string, std::string>> headers) const;

    S3::Settings settings_;
};

}  // namespace Storage
