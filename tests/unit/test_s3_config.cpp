/**
 * @file test_s3_config.cpp
 * @brief The S3 settings of the wallpaper library: complete or nothing, and
 *        the object keys and public URLs built from them.
 */

#include <cstdlib>
#include <fstream>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "storage/S3Client.hpp"
#include "storage/S3Config.hpp"
#include "utils/Config.hpp"

namespace {

constexpr const char* kConfigFile = "s3_settings_config.json";

nlohmann::json complete() {
    return nlohmann::json{{"storage",
                           {{"s3",
                             {{"endpoint", "https://storage.example.com"},
                              {"region", "ru-central1"},
                              {"bucket", "walls"},
                              {"prefix", "life-os"},
                              {"access_key_id", "AK"},
                              {"secret_access_key", "SK"},
                              {"public_base_url", "https://cdn.example.com/"},
                              {"path_style", true}}}}}};
}

void load(const nlohmann::json& cfg) {
    if (Config::is_initialized()) {
        Config::shutdown();
    }
    std::ofstream(kConfigFile) << cfg.dump();
    Config::initialize(kConfigFile);
}

}  // namespace

class S3SettingsTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (const char* v : {"S3_ENDPOINT",
                              "S3_REGION",
                              "S3_BUCKET",
                              "S3_PREFIX",
                              "S3_ACCESS_KEY_ID",
                              "S3_SECRET_ACCESS_KEY",
                              "S3_PUBLIC_BASE_URL",
                              "S3_PATH_STYLE"}) {
            unsetenv(v);
        }
        if (Config::is_initialized()) {
            Config::shutdown();
        }
    }

    void TearDown() override {
        if (Config::is_initialized()) {
            Config::shutdown();
        }
    }
};

TEST_F(S3SettingsTest, EmptyBucketMeansNoStore) {
    auto cfg = complete();
    cfg["storage"]["s3"]["bucket"] = "";
    load(cfg);
    EXPECT_FALSE(Storage::S3::settings_from_config().has_value());
}

TEST_F(S3SettingsTest, EmptyPublicBaseUrlMeansNoStore) {
    auto cfg = complete();
    cfg["storage"]["s3"]["public_base_url"] = "";
    load(cfg);
    EXPECT_FALSE(Storage::S3::settings_from_config().has_value());
}

TEST_F(S3SettingsTest, NoConfigMeansNoStore) {
    EXPECT_FALSE(Storage::S3::settings_from_config().has_value());
}

TEST_F(S3SettingsTest, CompleteSettingsAreNormalised) {
    load(complete());
    const auto s = Storage::S3::settings_from_config();
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->credentials.access_key_id, "AK");
    EXPECT_EQ(s->credentials.secret_access_key, "SK");
    EXPECT_EQ(s->credentials.region, "ru-central1");
    EXPECT_EQ(s->target.endpoint, "https://storage.example.com");
    EXPECT_EQ(s->target.bucket, "walls");
    EXPECT_TRUE(s->target.path_style);
    EXPECT_EQ(s->prefix, "life-os/");                          // a trailing slash added
    EXPECT_EQ(s->public_base_url, "https://cdn.example.com");  // the trailing slash trimmed
}

TEST_F(S3SettingsTest, KeysAndPublicUrls) {
    load(complete());
    const Storage::S3Client client(*Storage::S3::settings_from_config());
    const std::string key = client.key_for("wallpapers", "0b8e", "file.mp4");
    EXPECT_EQ(key, "life-os/wallpapers/0b8e/file.mp4");
    EXPECT_EQ(client.public_url(key), "https://cdn.example.com/life-os/wallpapers/0b8e/file.mp4");
    const std::string url = client.upload_url(key, "video/mp4", 1234);
    EXPECT_EQ(url.rfind("https://storage.example.com/walls/life-os/wallpapers/0b8e/file.mp4?", 0), 0u) << url;
    EXPECT_NE(url.find("X-Amz-Expires=900"), std::string::npos) << url;
}
