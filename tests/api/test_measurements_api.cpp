/**
 * @file test_measurements_api.cpp
 * @brief /api/v1/fitness/measurements: the module switch, no fitness
 *        permission needed (the rows are the caller's own), the checks of a measurement, add-list-edit-delete with the
 * newest date first, a range, the owner scope on every id route, and an edit that would clear every girth refused.
 */

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/MeasurementsController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaa050";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbb050";

Security::Auth::AuthPrincipal user(const char* who, std::uint32_t permissions) {
    Security::Auth::AuthPrincipal p;
    p.subject = who;
    p.raw_claims = json{{"sub", who}, {"permissions", permissions}};
    return p;
}

Security::Auth::AuthPrincipal reader(const char* who) {
    return user(who, Domain::Permission::kGeneral | Domain::Permission::kFitnessRead);
}

using TestHelpers::body_of;

class MeasurementsApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::MeasurementsController c;

    std::string config_file_name() const override { return "measurements_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["fitness"]["enabled"] = true; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            for (const char* id : {kAnna, kBoris}) {
                const std::string o = id;
                txn.exec_params("DELETE FROM tape_measurements WHERE owner_id = $1::uuid", o);
                txn.exec_params(
                    "INSERT INTO users (id, email, confirmed, role_id) "
                    "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                    "ON CONFLICT DO NOTHING",
                    o,
                    o + "@example.test");
            }
            return true;
        });
    }

    using Fn = std::function<void(const HttpRequestPtr&, Api::MeasurementsController::Callback&&)>;

    HttpResponsePtr call(const Security::Auth::AuthPrincipal& who,
                         HttpMethod method,
                         const json& body,
                         const Fn& fn,
                         const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto req = TestHelpers::authed_json(who, body, method);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        fn(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    HttpResponsePtr add(const char* who, const json& body) {
        auto r = call(reader(who), Post, body, [&](auto req, auto cb) { c.create(req, std::move(cb)); });
        TestHelpers::expect_2xx_matches_schema(r, "POST", "/api/v1/fitness/measurements");
        return r;
    }

    std::string added(const char* who, const json& body) {
        auto r = add(who, body);
        EXPECT_EQ(r->statusCode(), k201Created) << std::string(r->body());
        return body_of(r)["data"]["id"].get<std::string>();
    }

    json list(const char* who, const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto r = call(
            reader(who), Get, json::object(), [&](auto req, auto cb) { c.list(req, std::move(cb)); }, params);
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        TestHelpers::expect_2xx_matches_schema(r, "GET", "/api/v1/fitness/measurements");
        return body_of(r);
    }

    HttpResponsePtr edit(const char* who, const std::string& id, const json& body) {
        auto r = call(reader(who), Patch, body, [&](auto req, auto cb) { c.update(req, std::move(cb), id); });
        TestHelpers::expect_2xx_matches_schema(r, "PATCH", "/api/v1/fitness/measurements/{id}");
        return r;
    }
};

class MeasurementsOffTest : public MeasurementsApiTest {
protected:
    std::string config_file_name() const override { return "measurements_off_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["fitness"]["enabled"] = false; }
};

}  // namespace

TEST_F(MeasurementsOffTest, TheRoutesAreNotThereWhileFitnessIsOff) {
    auto r = call(reader(kAnna), Get, json::object(), [&](auto req, auto cb) { c.list(req, std::move(cb)); });
    EXPECT_EQ(r->statusCode(), k404NotFound);
}

TEST_F(MeasurementsApiTest, NoFitnessPermissionIsNeeded) {
    // A role without any fitness bit still keeps its own girths, as the medical archive.
    const auto plain = user(kAnna, Domain::Permission::kGeneral);
    auto made = call(plain, Post, json{{"date", "2026-10-18"}, {"waist_cm", 82}}, [&](auto req, auto cb) {
        c.create(req, std::move(cb));
    });
    ASSERT_EQ(made->statusCode(), k201Created) << std::string(made->body());
    TestHelpers::expect_matches_schema(made, "POST", "/api/v1/fitness/measurements");
    const std::string id = body_of(made)["data"]["id"].get<std::string>();

    auto listed = call(plain, Get, json::object(), [&](auto req, auto cb) { c.list(req, std::move(cb)); });
    ASSERT_EQ(listed->statusCode(), k200OK) << std::string(listed->body());
    TestHelpers::expect_matches_schema(listed, "GET", "/api/v1/fitness/measurements");
    const json rows = body_of(listed)["data"];
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0]["id"], id);
    EXPECT_EQ(rows[0]["waist_cm"], 82);

    auto edited =
        call(plain, Patch, json{{"waist_cm", 81}}, [&](auto req, auto cb) { c.update(req, std::move(cb), id); });
    EXPECT_EQ(edited->statusCode(), k200OK) << std::string(edited->body());
    auto gone = call(plain, Delete, json::object(), [&](auto req, auto cb) { c.remove(req, std::move(cb), id); });
    EXPECT_EQ(gone->statusCode(), k204NoContent);
}

TEST_F(MeasurementsApiTest, AMeasurementIsChecked) {
    for (const json& bad : {
             json{{"waist_cm", 82}},                               // no date
             json{{"date", "2026-10-18"}},                         // no girth
             json{{"date", "2026-10-18"}, {"waist_cm", nullptr}},  // a null girth is no girth
             json{{"date", "2026-02-30"}, {"waist_cm", 82}},       // not a calendar day
             json{{"date", "2026-10-18"}, {"waist_cm", 0}},        // above 0
             json{{"date", "2026-10-18"}, {"chest_cm", 400}},      // below 400
             json{{"date", "2026-10-18"}, {"neck_cm", "38"}},      // a number
             json{{"date", "2026-10-18"}, {"arm_cm", 34}, {"note", std::string(501, 'n')}},
         }) {
        EXPECT_EQ(add(kAnna, bad)->statusCode(), k400BadRequest) << bad.dump();
    }
    EXPECT_EQ(list(kAnna)["total"], 0);
}

TEST_F(MeasurementsApiTest, AddListEditDelete) {
    const auto older = added(kAnna, json{{"date", "2026-09-01"}, {"waist_cm", 84.5}, {"hips_cm", 98}});
    auto r = add(kAnna,
                 json{{"date", "2026-10-18"},
                      {"waist_cm", 82.25},
                      {"chest_cm", 101},
                      {"hips_cm", 97},
                      {"neck_cm", 38},
                      {"arm_cm", 34.5},
                      {"thigh_cm", 56},
                      {"note", "Morning, before breakfast"}});
    ASSERT_EQ(r->statusCode(), k201Created) << std::string(r->body());
    const json m = body_of(r)["data"];
    const auto newer = m["id"].get<std::string>();
    EXPECT_EQ(m["date"], "2026-10-18");
    EXPECT_DOUBLE_EQ(m["waist_cm"].get<double>(), 82.3) << "kept to one decimal (numeric(5,1))";
    EXPECT_EQ(m["thigh_cm"], 56);
    EXPECT_EQ(m["note"], "Morning, before breakfast");

    auto all = list(kAnna);
    EXPECT_EQ(all["total"], 2);
    ASSERT_EQ(all["data"].size(), 2U);
    EXPECT_EQ(all["data"][0]["id"], newer) << "the newest date first";
    EXPECT_TRUE(all["data"][1]["chest_cm"].is_null()) << "not measured that day";
    EXPECT_EQ(list(kAnna, {{"from", "2026-10-01"}})["total"], 1);
    EXPECT_EQ(list(kAnna, {{"to", "2026-09-30"}})["data"][0]["id"], older);
    EXPECT_EQ(call(reader(kAnna),
                   Get,
                   json::object(),
                   [&](auto req, auto cb) { c.list(req, std::move(cb)); },
                   {{"from", "2026-10-18"}, {"to", "2026-10-01"}})
                  ->statusCode(),
              k400BadRequest);

    // An edit changes what it names; a girth sent as null is cleared.
    auto e = edit(kAnna, older, json{{"chest_cm", 102}, {"hips_cm", nullptr}, {"note", "Evening"}});
    ASSERT_EQ(e->statusCode(), k200OK) << std::string(e->body());
    EXPECT_EQ(body_of(e)["data"]["chest_cm"], 102);
    EXPECT_TRUE(body_of(e)["data"]["hips_cm"].is_null());
    EXPECT_EQ(body_of(e)["data"]["waist_cm"], 84.5);
    EXPECT_EQ(body_of(e)["data"]["date"], "2026-09-01");
    EXPECT_EQ(edit(kAnna, older, json{{"date", "2026-09-02"}})->statusCode(), k200OK);
    EXPECT_EQ(edit(kAnna, older, json{{"date", nullptr}})->statusCode(), k400BadRequest);
    EXPECT_EQ(edit(kAnna, older, json{{"waist_cm", nullptr}, {"chest_cm", nullptr}})->statusCode(), k400BadRequest)
        << "a measurement keeps at least one girth";

    EXPECT_EQ(
        call(reader(kAnna), Delete, json::object(), [&](auto req, auto cb) { c.remove(req, std::move(cb), older); })
            ->statusCode(),
        k204NoContent);
    EXPECT_EQ(list(kAnna)["total"], 1);
}

TEST_F(MeasurementsApiTest, AnotherOwnersMeasurementIsNotFound) {
    const auto id = added(kAnna, json{{"date", "2026-10-18"}, {"waist_cm", 82}});
    EXPECT_EQ(edit(kBoris, id, json{{"waist_cm", 70}})->statusCode(), k404NotFound);
    EXPECT_EQ(call(reader(kBoris), Delete, json::object(), [&](auto req, auto cb) { c.remove(req, std::move(cb), id); })
                  ->statusCode(),
              k404NotFound);
    EXPECT_EQ(list(kBoris)["total"], 0);
    EXPECT_EQ(list(kAnna)["data"][0]["waist_cm"], 82) << "Anna's row is untouched";
    EXPECT_EQ(edit(kAnna, "not-an-id", json{{"waist_cm", 70}})->statusCode(), k400BadRequest);
}
