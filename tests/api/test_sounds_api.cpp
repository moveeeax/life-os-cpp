/**
 * @file test_sounds_api.cpp
 * @brief The focus sound library: admin-only uploads through presigned URLs,
 *        ready only when the file is in the bucket at the declared size, the
 *        public list of ready sounds, delete, and 503 without storage. The
 *        bucket is a fake ObjectStore.
 */

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/SoundsController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "storage/ObjectStore.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAdmin = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaf2";
constexpr const char* kUser = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbf2";
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
    std::string key_for(const std::string& folder, const std::string& id, const std::string& name) const override {
        return "life-os/" + folder + "/" + id + "/" + name;
    }
};

std::string key_of(const std::string& url) {
    return url.substr(std::string("https://fake/").size());
}

}  // namespace

class SoundsApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::SoundsController c;
    FakeStore store;

    std::string config_file_name() const override { return "sounds_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["now"]["enabled"] = true; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE sounds");
            return true;
        });
        Storage::install_object_store_for_testing(&store);
    }
    void TearDown() override {
        Storage::install_object_store_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    using Handler = void (Api::SoundsController::*)(const HttpRequestPtr&,
                                                    std::function<void(const HttpResponsePtr&)>&&);
    using IdHandler = void (Api::SoundsController::*)(const HttpRequestPtr&,
                                                      std::function<void(const HttpResponsePtr&)>&&,
                                                      const std::string&);

    HttpResponsePtr call(Handler h,
                         const Security::Auth::AuthPrincipal* who,
                         HttpMethod m,
                         const json& body = json::object()) {
        auto req = who ? TestHelpers::authed_json(*who, body, m) : TestHelpers::make_request(m, body);
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
    json rain() {
        return json{{"title", "Rain on the roof"},
                    {"content_type", "audio/mpeg"},
                    {"bytes", 8 * kMb},
                    {"duration_seconds", 600}};
    }
    json create(const json& body) {
        const auto a = admin();
        auto r = call(&Api::SoundsController::createSound, &a, Post, body);
        EXPECT_EQ(r->statusCode(), k201Created) << std::string(r->body());
        return body_of(r)["data"];
    }
    json listed() {
        const auto u = user();
        auto r = call(&Api::SoundsController::listSounds, &u, Get);
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        return body_of(r)["data"];
    }
};

TEST_F(SoundsApiTest, AdminRoutesRefuseAPlainUser) {
    const auto u = user();
    EXPECT_EQ(call(&Api::SoundsController::createSound, &u, Post, rain())->statusCode(), k403Forbidden);
    EXPECT_EQ(call(&Api::SoundsController::listAdmin, &u, Get)->statusCode(), k403Forbidden);
    const std::string id = create(rain())["id"];
    EXPECT_EQ(call_id(&Api::SoundsController::markReady, u, Post, id)->statusCode(), k403Forbidden);
    EXPECT_EQ(call_id(&Api::SoundsController::deleteSound, u, Delete, id)->statusCode(), k403Forbidden);
}

TEST_F(SoundsApiTest, NoStoreAnswers503) {
    Storage::install_object_store_for_testing(nullptr);
    const auto a = admin();
    auto r = call(&Api::SoundsController::createSound, &a, Post, rain());
    EXPECT_EQ(r->statusCode(), k503ServiceUnavailable);
    EXPECT_EQ(body_of(r)["error"], "storage_not_configured");
}

TEST_F(SoundsApiTest, ASoundGoesUnderTheSoundsFolder) {
    const json d = create(rain());
    EXPECT_EQ(key_of(d["upload"]["url"]), "life-os/sounds/" + d["id"].get<std::string>() + "/file.mp3");
    EXPECT_EQ(d["upload"]["headers"]["Content-Type"], "audio/mpeg");
    EXPECT_EQ(d["upload"]["headers"]["Cache-Control"], "public, max-age=31536000, immutable");
}

TEST_F(SoundsApiTest, CreateRefusesWhatTheLibraryDoesNotTake) {
    const auto a = admin();
    const auto refused = [&](json body, const char* field) {
        auto r = call(&Api::SoundsController::createSound, &a, Post, body);
        EXPECT_EQ(r->statusCode(), k400BadRequest) << field << " " << std::string(r->body());
        EXPECT_NE(std::string(r->body()).find(field), std::string::npos) << field;
    };
    json big = rain();
    big["bytes"] = 30 * kMb + 1;
    refused(big, "bytes");
    json wav = rain();
    wav["content_type"] = "audio/wav";
    refused(wav, "content_type");
    json untitled = rain();
    untitled["title"] = "";
    refused(untitled, "title");
    json long_title = rain();
    long_title["title"] = std::string(81, 'x');
    refused(long_title, "title");
}

TEST_F(SoundsApiTest, ReadyNeedsTheFileAtItsSizeAndOnlyReadyAreListed) {
    const json d = create(rain());
    const std::string id = d["id"];
    EXPECT_EQ(call_id(&Api::SoundsController::markReady, admin(), Post, id)->statusCode(), k409Conflict);
    EXPECT_TRUE(listed().empty());
    store.objects[key_of(d["upload"]["url"])] = 8 * kMb;
    auto r = call_id(&Api::SoundsController::markReady, admin(), Post, id);
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    const json l = listed();
    ASSERT_EQ(l.size(), 1u);
    EXPECT_EQ(l[0]["title"], "Rain on the roof");
    EXPECT_EQ(l[0]["url"], "https://cdn.test/" + key_of(d["upload"]["url"]));
    EXPECT_EQ(l[0]["duration_seconds"], 600);
}

TEST_F(SoundsApiTest, DeleteRemovesTheFileAndTheRow) {
    const std::string id = create(rain())["id"];
    EXPECT_EQ(call_id(&Api::SoundsController::deleteSound, admin(), Delete, id)->statusCode(), k204NoContent);
    EXPECT_EQ(store.removed.size(), 1u);
    EXPECT_EQ(call_id(&Api::SoundsController::deleteSound, admin(), Delete, id)->statusCode(), k404NotFound);
}
