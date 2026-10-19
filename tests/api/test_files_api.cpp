/**
 * @file test_files_api.cpp
 * @brief /api/v1/files: uploads under the private prefix only, ready only
 *        when the object is there at the declared size and the store refuses
 *        an unsigned read, a short presigned GET for the owner, 404 for
 *        another owner on the row, the list and the download link, 503 when
 *        storage is missing or refuses, and the purge of objects when a file,
 *        a record or a travel document goes (files_sweep). The bucket is a fake.
 */

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/FilesController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "files/jobs/Sweep.hpp"
#include "storage/ObjectStore.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaa0f21";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbb0f22";

Security::Auth::AuthPrincipal principal(const char* who) {
    Security::Auth::AuthPrincipal p;
    p.subject = who;
    p.raw_claims = json{{"sub", who}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

using TestHelpers::body_of;
using Fn = std::function<void(const HttpRequestPtr&, Api::FilesController::Callback&&)>;

/// The bucket as a map key → size; an upload is "done" by writing the map.
class FakeStore : public Storage::ObjectStore {
public:
    std::map<std::string, long long> objects;
    std::set<std::string> public_keys;  // keys an unsigned HEAD would read
    mutable std::vector<std::string> removed;
    mutable std::vector<std::pair<std::string, std::string>> signed_downloads;  // key, disposition
    bool fail_head = false;
    bool fail_remove = false;

    std::string upload_url(const std::string& key, const std::string&, long long) const override {
        return "https://fake/public-put/" + key;
    }
    Storage::ObjectInfo head(const std::string& key) const override {
        if (fail_head) {
            throw std::runtime_error("s3: HEAD answered HTTP 403");
        }
        const auto it = objects.find(key);
        return it == objects.end() ? Storage::ObjectInfo{false, 0} : Storage::ObjectInfo{true, it->second};
    }
    void remove(const std::string& key) const override {
        if (fail_remove) {
            throw std::runtime_error("s3: DELETE answered HTTP 503");
        }
        removed.push_back(key);
    }
    std::string public_url(const std::string& key) const override { return "https://cdn.test/" + key; }
    std::string key_for(const std::string& folder, const std::string& id, const std::string& name) const override {
        return "life-os/" + folder + "/" + id + "/" + name;
    }
    std::string private_key(const std::string& owner, const std::string& id, const std::string& name) const override {
        return "life-os/private/" + owner + "/" + id + "/" + name;
    }
    std::string private_upload_url(const std::string& key, const std::string&, long long) const override {
        return "https://fake/put/" + key;
    }
    std::string download_url(const std::string& key, long seconds, const std::string& disposition) const override {
        signed_downloads.emplace_back(key, disposition);
        return "https://fake/get/" + key + "?X-Amz-Expires=" + std::to_string(seconds);
    }
    bool publicly_readable(const std::string& key) const override { return public_keys.count(key) > 0; }
};

std::string key_of_put(const std::string& url) {
    return url.substr(std::string("https://fake/put/").size());
}

long long count_of(const std::string& sql) {
    return Database::get().execute_read([&](auto& txn) { return txn.exec(sql)[0][0].template as<long long>(); });
}

class FilesApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::FilesController c;
    FakeStore store;
    bool travel_on = true;

    std::string config_file_name() const override { return "files_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["medical"]["enabled"] = true;
        cfg["travel"]["enabled"] = travel_on;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            for (const char* id : {kAnna, kBoris}) {
                const std::string o = id;
                for (const char* table : {"private_files", "medical_records", "medical_people", "travel_documents"}) {
                    txn.exec_params(std::string("DELETE FROM ") + table + " WHERE owner_id = $1::uuid", o);
                }
            }
            txn.exec("DELETE FROM private_file_purges WHERE object_key LIKE 'life-os/private/%'");
            return true;
        });
        Storage::install_object_store_for_testing(&store);
    }

    void TearDown() override {
        Storage::install_object_store_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    HttpResponsePtr call(const char* who,
                         HttpMethod method,
                         const json& body,
                         const Fn& fn,
                         const char* spec_path = nullptr,
                         const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto req = TestHelpers::authed_json(principal(who), body, method);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        fn(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        if (captured && spec_path) {
            TestHelpers::expect_2xx_matches_schema(captured, method, spec_path);
        }
        return captured;
    }

    /// A medical record of @p who; answers its id.
    static std::string record_of(const char* who) {
        return Database::get().execute_write([&](auto& txn) {
            const std::string o = who;
            auto p = txn.exec_params(
                "INSERT INTO medical_people (owner_id, name) VALUES ($1::uuid, 'Anna') RETURNING id::text", o);
            auto r = txn.exec_params(
                "INSERT INTO medical_records (owner_id, person_id, date, kind, title) "
                "VALUES ($1::uuid, $2::uuid, '2026-10-01', 'lab', 'Blood') RETURNING id::text",
                o,
                p[0][0].template as<std::string>());
            return r[0][0].template as<std::string>();
        });
    }

    static std::string document_of(const char* who) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "INSERT INTO travel_documents (owner_id, holder, kind, valid_until) "
                "VALUES ($1::uuid, 'Anna', 'passport', '2030-01-01') RETURNING id::text",
                std::string(who));
            return r[0][0].template as<std::string>();
        });
    }

    static json start(const std::string& kind,
                      const std::string& parent,
                      const std::string& name = "blood test.pdf",
                      long long bytes = 1000) {
        return json{{"parent_kind", kind},
                    {"parent_id", parent},
                    {"name", name},
                    {"content_type", "application/pdf"},
                    {"bytes", bytes}};
    }

    /// Creates a file and answers the create's data.
    json create(const char* who, const std::string& kind, const std::string& parent, long long bytes = 1000) {
        auto r = call(
            who,
            Post,
            start(kind, parent, "blood test.pdf", bytes),
            [&](auto req, auto cb) { c.createFile(req, std::move(cb)); },
            "/api/v1/files");
        EXPECT_EQ(r->statusCode(), k201Created) << std::string(r->body());
        return body_of(r)["data"];
    }

    HttpResponsePtr ready(const char* who, const std::string& id) {
        return call(
            who,
            Post,
            json::object(),
            [&](auto req, auto cb) { c.markReady(req, std::move(cb), id); },
            "/api/v1/files/{id}/ready");
    }

    /// A file uploaded and ready; answers its id and key.
    std::pair<std::string, std::string> uploaded(const char* who, const std::string& kind, const std::string& parent) {
        const json d = create(who, kind, parent);
        const std::string key = key_of_put(d["upload"]["url"].get<std::string>());
        store.objects[key] = 1000;
        const std::string id = d["file"]["id"].get<std::string>();
        EXPECT_EQ(ready(who, id)->statusCode(), k200OK);
        return {id, key};
    }
};

class FilesTravelOffTest : public FilesApiTest {
protected:
    FilesTravelOffTest() { travel_on = false; }
    std::string config_file_name() const override { return "files_travel_off_test_config.json"; }
};

}  // namespace

TEST_F(FilesApiTest, AnUploadGoesUnderThePrivatePrefixOfItsOwner) {
    const std::string rec = record_of(kAnna);
    const json d = create(kAnna, "medical_record", rec);
    const std::string key = key_of_put(d["upload"]["url"].get<std::string>());
    EXPECT_EQ(key.rfind(std::string("life-os/private/") + kAnna + "/", 0), 0u) << key;
    EXPECT_EQ(key.substr(key.size() - 9), "/file.pdf");
    EXPECT_EQ(d["upload"]["headers"]["Cache-Control"], "private, no-store");
    EXPECT_EQ(d["upload"]["headers"]["Content-Type"], "application/pdf");
    EXPECT_EQ(d["file"]["status"], "pending");
    EXPECT_FALSE(d["file"].contains("object_key"));
    EXPECT_EQ(d["file"]["name"], "blood test.pdf");
}

TEST_F(FilesApiTest, TheNameKeepsItsLastSegmentAndBadInputIsRefused) {
    const std::string rec = record_of(kAnna);
    auto r = call(kAnna, Post, start("medical_record", rec, "C:\\scans\\x-ray.pdf"), [&](auto req, auto cb) {
        c.createFile(req, std::move(cb));
    });
    ASSERT_EQ(r->statusCode(), k201Created) << std::string(r->body());
    EXPECT_EQ(body_of(r)["data"]["file"]["name"], "x-ray.pdf");

    json exe = start("medical_record", rec);
    exe["content_type"] = "application/x-msdownload";
    for (const json& bad : {exe,
                            start("medical_record", rec, "a.pdf", 26LL * 1024 * 1024),
                            start("medical_record", rec, "a.pdf", 0),
                            start("trip", rec),
                            start("medical_record", "not-a-uuid")}) {
        auto b = call(kAnna, Post, bad, [&](auto req, auto cb) { c.createFile(req, std::move(cb)); });
        EXPECT_EQ(b->statusCode(), k400BadRequest) << bad.dump();
    }
}

TEST_F(FilesApiTest, AnotherOwnersParentIsNotFound) {
    const std::string theirs = record_of(kBoris);
    auto r = call(
        kAnna, Post, start("medical_record", theirs), [&](auto req, auto cb) { c.createFile(req, std::move(cb)); });
    EXPECT_EQ(r->statusCode(), k404NotFound) << std::string(r->body());
    auto list = call(kAnna,
                     Get,
                     json::object(),
                     [&](auto req, auto cb) { c.listFiles(req, std::move(cb)); },
                     nullptr,
                     {{"parent_kind", "medical_record"}, {"parent_id", theirs}});
    EXPECT_EQ(list->statusCode(), k404NotFound);
}

TEST_F(FilesApiTest, ReadyNeedsTheObjectAtItsSize) {
    const std::string rec = record_of(kAnna);
    const json d = create(kAnna, "medical_record", rec);
    const std::string id = d["file"]["id"].get<std::string>();
    const std::string key = key_of_put(d["upload"]["url"].get<std::string>());
    auto missing = ready(kAnna, id);
    ASSERT_EQ(missing->statusCode(), k409Conflict);
    EXPECT_EQ(body_of(missing)["error"], "upload_incomplete");
    store.objects[key] = 999;
    EXPECT_EQ(ready(kAnna, id)->statusCode(), k409Conflict);
    store.objects[key] = 1000;
    auto ok = ready(kAnna, id);
    ASSERT_EQ(ok->statusCode(), k200OK) << std::string(ok->body());
    EXPECT_EQ(body_of(ok)["data"]["status"], "ready");

    auto list = call(kAnna,
                     Get,
                     json::object(),
                     [&](auto req, auto cb) { c.listFiles(req, std::move(cb)); },
                     "/api/v1/files",
                     {{"parent_kind", "medical_record"}, {"parent_id", rec}});
    ASSERT_EQ(list->statusCode(), k200OK);
    ASSERT_EQ(body_of(list)["data"].size(), 1u);
    EXPECT_FALSE(body_of(list)["data"][0].contains("object_key"));
    EXPECT_EQ(count_of("SELECT files FROM (SELECT (SELECT count(*) FROM private_files f WHERE f.parent_id = r.id "
                       " AND f.status = 'ready') AS files FROM medical_records r WHERE r.id = '" +
                       rec + "') t"),
              1);
}

TEST_F(FilesApiTest, AFileOpenToAnonymousReadersIsDeletedNotReady) {
    const std::string rec = record_of(kAnna);
    const json d = create(kAnna, "medical_record", rec);
    const std::string id = d["file"]["id"].get<std::string>();
    const std::string key = key_of_put(d["upload"]["url"].get<std::string>());
    store.objects[key] = 1000;
    store.public_keys.insert(key);
    auto r = ready(kAnna, id);
    ASSERT_EQ(r->statusCode(), k503ServiceUnavailable) << std::string(r->body());
    EXPECT_EQ(body_of(r)["error"], "storage_public");
    EXPECT_EQ(count_of("SELECT count(*) FROM private_files WHERE id = '" + id + "'"), 0);
    ASSERT_EQ(store.removed.size(), 1u);
    EXPECT_EQ(store.removed[0], key);
}

TEST_F(FilesApiTest, AStoreThatRefusesAnswers503) {
    const std::string rec = record_of(kAnna);
    const json d = create(kAnna, "medical_record", rec);
    store.fail_head = true;
    auto r = ready(kAnna, d["file"]["id"].get<std::string>());
    ASSERT_EQ(r->statusCode(), k503ServiceUnavailable);
    EXPECT_EQ(body_of(r)["error"], "storage_unavailable");

    Storage::install_object_store_for_testing(nullptr);  // the configured store: none in tests
    auto none =
        call(kAnna, Post, start("medical_record", rec), [&](auto req, auto cb) { c.createFile(req, std::move(cb)); });
    ASSERT_EQ(none->statusCode(), k503ServiceUnavailable);
    EXPECT_EQ(body_of(none)["error"], "storage_not_configured");
}

TEST_F(FilesApiTest, TheDownloadIsAShortLinkForTheOwnerOnly) {
    const std::string rec = record_of(kAnna);
    const auto up = uploaded(kAnna, "medical_record", rec);
    const std::string id = up.first;
    const std::string key = up.second;
    auto r = call(
        kAnna,
        Get,
        json::object(),
        [&](auto req, auto cb) { c.downloadFile(req, std::move(cb), id); },
        "/api/v1/files/{id}/download");
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    EXPECT_EQ(body_of(r)["data"]["expires_in"], 120);
    EXPECT_EQ(body_of(r)["data"]["url"], "https://fake/get/" + key + "?X-Amz-Expires=120");
    EXPECT_EQ(r->getHeader("Cache-Control"), "no-store");
    ASSERT_EQ(store.signed_downloads.size(), 1u);
    EXPECT_EQ(store.signed_downloads[0].second,
              "inline; filename=\"blood test.pdf\"; filename*=UTF-8''blood%20test.pdf");

    for (auto* fn : {+[](Api::FilesController& fc,
                         const HttpRequestPtr& req,
                         Api::FilesController::Callback&& cb,
                         const std::string& fid) { fc.downloadFile(req, std::move(cb), fid); },
                     +[](Api::FilesController& fc,
                         const HttpRequestPtr& req,
                         Api::FilesController::Callback&& cb,
                         const std::string& fid) { fc.markReady(req, std::move(cb), fid); },
                     +[](Api::FilesController& fc,
                         const HttpRequestPtr& req,
                         Api::FilesController::Callback&& cb,
                         const std::string& fid) { fc.deleteFile(req, std::move(cb), fid); }}) {
        auto theirs = call(kBoris, Get, json::object(), [&](auto req, auto cb) { fn(c, req, std::move(cb), id); });
        EXPECT_EQ(theirs->statusCode(), k404NotFound) << std::string(theirs->body());
    }
    EXPECT_EQ(store.signed_downloads.size(), 1u) << "no link was signed for another owner";
    EXPECT_EQ(count_of("SELECT count(*) FROM private_files WHERE id = '" + id + "'"), 1);
}

TEST_F(FilesApiTest, APendingFileHasNoLink) {
    const std::string rec = record_of(kAnna);
    const json d = create(kAnna, "medical_record", rec);
    const std::string id = d["file"]["id"].get<std::string>();
    auto r = call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.downloadFile(req, std::move(cb), id); });
    ASSERT_EQ(r->statusCode(), k409Conflict);
    EXPECT_EQ(body_of(r)["error"], "not_ready");
}

TEST_F(FilesApiTest, ADeleteDropsTheRowFirstAndKeepsTheKeyForTheSweep) {
    const std::string rec = record_of(kAnna);
    const auto up = uploaded(kAnna, "medical_record", rec);
    const std::string id = up.first;
    const std::string key = up.second;
    store.fail_remove = true;
    auto r = call(kAnna, Delete, json::object(), [&](auto req, auto cb) { c.deleteFile(req, std::move(cb), id); });
    ASSERT_EQ(r->statusCode(), k204NoContent);
    EXPECT_EQ(count_of("SELECT count(*) FROM private_files WHERE id = '" + id + "'"), 0);
    EXPECT_EQ(count_of("SELECT count(*) FROM private_file_purges WHERE object_key = '" + key + "'"), 1);

    // The sweep takes a purge only after the upload window; age it past that.
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("UPDATE private_file_purges SET created_at = now() - interval '2 hours' WHERE object_key = $1",
                        key);
        return true;
    });
    store.fail_remove = false;
    const json out = Jobs::FilesSweep::process_job(json::object());
    EXPECT_EQ(out["status"], "done");
    EXPECT_GE(out["removed"].get<int>(), 1);
    EXPECT_NE(std::find(store.removed.begin(), store.removed.end(), key), store.removed.end());
    EXPECT_EQ(count_of("SELECT count(*) FROM private_file_purges WHERE object_key = '" + key + "'"), 0);
}

TEST_F(FilesApiTest, TheSweepDropsStalePendingUploads) {
    const std::string rec = record_of(kAnna);
    const json d = create(kAnna, "medical_record", rec);
    const std::string id = d["file"]["id"].get<std::string>();
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("UPDATE private_files SET created_at = now() - interval '2 hours' WHERE id = $1::uuid", id);
        return true;
    });
    const json out = Jobs::FilesSweep::process_job(json::object());
    EXPECT_GE(out["stale"].get<int>(), 1);
    EXPECT_EQ(count_of("SELECT count(*) FROM private_files WHERE id = '" + id + "'"), 0);
    EXPECT_EQ(count_of("SELECT count(*) FROM private_file_purges WHERE object_key LIKE '%" + id + "%'"), 1)
        << "the key waits for the window of its presigned PUT";
}

TEST_F(FilesApiTest, ARecordOrADocumentTakesItsFilesAlong) {
    const std::string rec = record_of(kAnna);
    const auto r_up = uploaded(kAnna, "medical_record", rec);
    const std::string rid = r_up.first;
    const std::string rkey = r_up.second;
    const std::string doc = document_of(kAnna);
    const auto d_up = uploaded(kAnna, "travel_document", doc);
    const std::string did = d_up.first;
    const std::string dkey = d_up.second;
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("DELETE FROM medical_records WHERE id = $1::uuid", rec);
        txn.exec_params("DELETE FROM travel_documents WHERE id = $1::uuid", doc);
        return true;
    });
    EXPECT_EQ(count_of("SELECT count(*) FROM private_files WHERE id IN ('" + rid + "', '" + did + "')"), 0);
    EXPECT_EQ(count_of("SELECT count(*) FROM private_file_purges WHERE object_key IN ('" + rkey + "', '" + dkey + "')"),
              2);
}

TEST_F(FilesTravelOffTest, ATravelDocumentFileIs404WhileTravelIsOff) {
    const std::string doc = document_of(kAnna);
    auto r =
        call(kAnna, Post, start("travel_document", doc), [&](auto req, auto cb) { c.createFile(req, std::move(cb)); });
    ASSERT_EQ(r->statusCode(), k404NotFound);
    EXPECT_EQ(body_of(r)["message"], "travel not found");
}
