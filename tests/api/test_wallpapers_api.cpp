/**
 * @file test_wallpapers_api.cpp
 * @brief The wallpaper library routes: admin-only uploads through presigned
 *        URLs, ready only when both objects are in the bucket at the declared
 *        sizes, the public list of ready rows per device, delete, and 503
 *        while storage is not configured. The bucket is a fake ObjectStore.
 */

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/WallpapersController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "storage/ObjectStore.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAdmin = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaf1";
constexpr const char* kUser = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbf1";
constexpr long long kMb = 1024 * 1024;

Security::Auth::AuthPrincipal principal(const std::string& id, std::uint32_t permissions) {
    Security::Auth::AuthPrincipal p;
    p.subject = id;
    p.raw_claims = json{{"sub", id}, {"permissions", permissions}};
    return p;
}

Security::Auth::AuthPrincipal admin() {
    return principal(kAdmin, Domain::Permission::kAdminister | Domain::Permission::kGeneral);
}

Security::Auth::AuthPrincipal user() {
    return principal(kUser, Domain::Permission::kGeneral);
}

json body_of(const HttpResponsePtr& resp) {
    return json::parse(std::string(resp->body()));
}

/// The bucket as a map key → size; uploads are "done" by writing the map.
class FakeStore : public Storage::ObjectStore {
public:
    std::map<std::string, long long> objects;
    mutable std::vector<std::string> removed;

    std::string upload_url(const std::string& key, const std::string&, long long) const override {
        return "https://fake/" + key;
    }
    Storage::ObjectInfo head(const std::string& key) const override {
        const auto it = objects.find(key);
        return it == objects.end() ? Storage::ObjectInfo{false, 0} : Storage::ObjectInfo{true, it->second};
    }
    void remove(const std::string& key) const override { removed.push_back(key); }
    std::string public_url(const std::string& key) const override { return "https://cdn.test/" + key; }
    std::string key_for(const std::string& id, const std::string& name) const override {
        return "life-os/wallpapers/" + id + "/" + name;
    }
};

std::string key_of(const std::string& url) {
    return url.substr(std::string("https://fake/").size());
}

}  // namespace

class WallpapersApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::WallpapersController c;
    FakeStore store;

    std::string config_file_name() const override { return "wallpapers_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["now"]["enabled"] = true; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE wallpapers");
            return true;
        });
        Storage::install_object_store_for_testing(&store);
    }

    void TearDown() override {
        Storage::install_object_store_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    using Handler = void (Api::WallpapersController::*)(const HttpRequestPtr&,
                                                        std::function<void(const HttpResponsePtr&)>&&);
    using IdHandler = void (Api::WallpapersController::*)(const HttpRequestPtr&,
                                                          std::function<void(const HttpResponsePtr&)>&&,
                                                          const std::string&);

    HttpResponsePtr call(Handler h,
                         const Security::Auth::AuthPrincipal* who,
                         HttpMethod m,
                         const json& body = json::object(),
                         const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto req = who ? TestHelpers::authed_json(*who, body, m) : TestHelpers::make_request(m, body);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        (c.*h)(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    HttpResponsePtr call_id(IdHandler h,
                            const Security::Auth::AuthPrincipal& who,
                            HttpMethod m,
                            const std::string& id) {
        auto req = TestHelpers::authed_json(who, json::object(), m);
        HttpResponsePtr captured;
        (c.*h)(
            req, [&](const HttpResponsePtr& r) { captured = r; }, id);
        EXPECT_TRUE(captured);
        return captured;
    }

    json video(const std::string& device = "desktop") {
        return json{{"kind", "video"},
                    {"device", device},
                    {"content_type", "video/mp4"},
                    {"bytes", 40 * kMb},
                    {"poster_bytes", 200000},
                    {"width", 1920},
                    {"height", 1080}};
    }

    json image(const std::string& device) {
        return json{{"kind", "image"}, {"device", device}, {"content_type", "image/jpeg"}, {"bytes", 2 * kMb}};
    }

    /// Creates a row as the admin; returns the create answer's data.
    json create(const json& body) {
        const auto a = admin();
        auto r = call(&Api::WallpapersController::createWallpaper, &a, Post, body);
        EXPECT_EQ(r->statusCode(), k201Created) << std::string(r->body());
        return body_of(r)["data"];
    }

    HttpResponsePtr ready(const std::string& id) {
        return call_id(&Api::WallpapersController::markReady, admin(), Post, id);
    }

    json listed(const std::string& device) {
        const auto u = user();
        auto r = call(&Api::WallpapersController::listWallpapers, &u, Get, json::object(), {{"device", device}});
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        return body_of(r)["data"];
    }
};

TEST_F(WallpapersApiTest, AdminRoutesRefuseAPlainUser) {
    const auto u = user();
    EXPECT_EQ(call(&Api::WallpapersController::createWallpaper, &u, Post, video())->statusCode(), k403Forbidden);
    EXPECT_EQ(call(&Api::WallpapersController::listAdmin, &u, Get)->statusCode(), k403Forbidden);
    const std::string id = create(video())["id"];
    EXPECT_EQ(call_id(&Api::WallpapersController::markReady, u, Post, id)->statusCode(), k403Forbidden);
    EXPECT_EQ(call_id(&Api::WallpapersController::deleteWallpaper, u, Delete, id)->statusCode(), k403Forbidden);
}

TEST_F(WallpapersApiTest, ThePublicListNeedsASignedInUser) {
    EXPECT_EQ(call(&Api::WallpapersController::listWallpapers, nullptr, Get, json::object(), {{"device", "desktop"}})
                  ->statusCode(),
              k401Unauthorized);
}

TEST_F(WallpapersApiTest, NoStoreAnswers503) {
    Storage::install_object_store_for_testing(nullptr);
    const auto a = admin();
    auto r = call(&Api::WallpapersController::createWallpaper, &a, Post, video());
    EXPECT_EQ(r->statusCode(), k503ServiceUnavailable);
    EXPECT_EQ(body_of(r)["error"], "storage_not_configured") << std::string(r->body());
    const auto u = user();
    EXPECT_EQ(call(&Api::WallpapersController::listWallpapers, &u, Get, json::object(), {{"device", "desktop"}})
                  ->statusCode(),
              k503ServiceUnavailable);
}

class WallpapersOffTest : public WallpapersApiTest {
protected:
    std::string config_file_name() const override { return "wallpapers_off_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["now"]["enabled"] = false; }
};

TEST_F(WallpapersOffTest, ModuleOffAnswers404) {
    const auto a = admin();
    EXPECT_EQ(call(&Api::WallpapersController::createWallpaper, &a, Post, video())->statusCode(), k404NotFound);
}

TEST_F(WallpapersApiTest, CreateAVideoGivesTwoUploadUrls) {
    const json d = create(video());
    EXPECT_FALSE(d["id"].get<std::string>().empty());
    EXPECT_EQ(d["upload"]["url"], "https://fake/life-os/wallpapers/" + d["id"].get<std::string>() + "/file.mp4");
    EXPECT_EQ(d["upload"]["headers"]["Content-Type"], "video/mp4");
    EXPECT_EQ(d["poster_upload"]["url"],
              "https://fake/life-os/wallpapers/" + d["id"].get<std::string>() + "/poster.jpg");
    EXPECT_EQ(d["poster_upload"]["headers"]["Content-Type"], "image/jpeg");
}

TEST_F(WallpapersApiTest, CreateAnImageHasNoPoster) {
    const json d = create(image("phone"));
    EXPECT_TRUE(d["poster_upload"].is_null());
    EXPECT_EQ(key_of(d["upload"]["url"]), "life-os/wallpapers/" + d["id"].get<std::string>() + "/file.jpg");
}

TEST_F(WallpapersApiTest, CreateRefusesWhatTheLibraryDoesNotTake) {
    const auto a = admin();
    const auto refused = [&](json body, const char* field) {
        auto r = call(&Api::WallpapersController::createWallpaper, &a, Post, body);
        EXPECT_EQ(r->statusCode(), k400BadRequest) << field << " " << std::string(r->body());
        EXPECT_NE(std::string(r->body()).find(field), std::string::npos) << field << " " << std::string(r->body());
    };
    refused(video("phone"), "device");
    json big = video();
    big["bytes"] = 100 * kMb + 1;
    refused(big, "bytes");
    json png = image("desktop");
    png["content_type"] = "image/png";
    refused(png, "content_type");
    json with_poster = image("desktop");
    with_poster["poster_bytes"] = 1000;
    refused(with_poster, "poster_bytes");
    json no_poster = video();
    no_poster.erase("poster_bytes");
    refused(no_poster, "poster_bytes");
    json big_poster = video();
    big_poster["poster_bytes"] = kMb + 1;
    refused(big_poster, "poster_bytes");
    json big_image = image("phone");
    big_image["bytes"] = 10 * kMb + 1;
    refused(big_image, "bytes");
    json bad_kind = video();
    bad_kind["kind"] = "gif";
    refused(bad_kind, "kind");
}

TEST_F(WallpapersApiTest, ReadyNeedsBothObjectsAtTheDeclaredSizes) {
    const json d = create(video());
    const std::string id = d["id"];
    const std::string file = key_of(d["upload"]["url"]);
    const std::string poster = key_of(d["poster_upload"]["url"]);

    EXPECT_EQ(ready(id)->statusCode(), k409Conflict);  // nothing uploaded

    store.objects[file] = 40 * kMb;
    auto r = ready(id);
    EXPECT_EQ(r->statusCode(), k409Conflict);  // the poster is missing
    EXPECT_EQ(body_of(r)["error"], "upload_incomplete");

    store.objects[poster] = 200000;
    store.objects[file] = 40 * kMb - 5;  // a cut upload
    EXPECT_EQ(ready(id)->statusCode(), k409Conflict);
    EXPECT_TRUE(listed("desktop").empty());

    store.objects[file] = 40 * kMb;
    r = ready(id);
    EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    const json l = listed("desktop");
    ASSERT_EQ(l.size(), 1u);
    EXPECT_EQ(l[0]["id"], id);
    EXPECT_EQ(l[0]["kind"], "video");
    EXPECT_EQ(l[0]["url"], "https://cdn.test/" + file);
    EXPECT_EQ(l[0]["poster_url"], "https://cdn.test/" + poster);
    EXPECT_EQ(l[0]["width"], 1920);
}

TEST_F(WallpapersApiTest, ThePublicListIsPerDeviceAndReadyOnly) {
    const json phone = create(image("phone"));
    store.objects[key_of(phone["upload"]["url"])] = 2 * kMb;
    ASSERT_EQ(ready(phone["id"])->statusCode(), k200OK);
    create(image("desktop"));  // stays pending

    EXPECT_EQ(listed("phone").size(), 1u);
    EXPECT_TRUE(listed("desktop").empty());
    EXPECT_TRUE(listed("phone")[0]["poster_url"].is_null());

    const auto u = user();
    EXPECT_EQ(
        call(&Api::WallpapersController::listWallpapers, &u, Get, json::object(), {{"device", "tv"}})->statusCode(),
        k400BadRequest);
}

TEST_F(WallpapersApiTest, TheAdminListShowsPendingRowsNewestFirst) {
    const json first = create(image("phone"));
    const json second = create(video());
    const auto a = admin();
    auto r = call(&Api::WallpapersController::listAdmin, &a, Get);
    ASSERT_EQ(r->statusCode(), k200OK);
    const json l = body_of(r)["data"];
    ASSERT_EQ(l.size(), 2u);
    EXPECT_EQ(l[0]["id"], second["id"]);
    EXPECT_EQ(l[0]["status"], "pending");
    EXPECT_EQ(l[1]["id"], first["id"]);
}

TEST_F(WallpapersApiTest, DeleteRemovesBothObjectsAndTheRow) {
    const json d = create(video());
    const std::string id = d["id"];
    EXPECT_EQ(call_id(&Api::WallpapersController::deleteWallpaper, admin(), Delete, id)->statusCode(), k204NoContent);
    EXPECT_EQ(store.removed.size(), 2u);
    EXPECT_EQ(call_id(&Api::WallpapersController::deleteWallpaper, admin(), Delete, id)->statusCode(), k404NotFound);
    EXPECT_EQ(ready(id)->statusCode(), k404NotFound);
}
