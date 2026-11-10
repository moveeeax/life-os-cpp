/**
 * @file test_fitness_goals_api.cpp
 * @brief /api/v1/fitness/goals: the default steps goal while unset, set and
 *        cleared back with null, the range check, the owner scope, no fitness
 *        permission or linked band needed, and 404 while the module is off.
 */

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/FitnessGoalsController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaa085";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbb085";

Security::Auth::AuthPrincipal plain(const char* who) {
    Security::Auth::AuthPrincipal p;
    p.subject = who;
    p.raw_claims = json{{"sub", who}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

using TestHelpers::body_of;

class FitnessGoalsApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::FitnessGoalsController c;

    std::string config_file_name() const override { return "fitness_goals_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["fitness"]["enabled"] = true; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            for (const char* id : {kAnna, kBoris}) {
                const std::string o = id;
                txn.exec_params("DELETE FROM fitness_goals WHERE owner_id = $1::uuid", o);
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

    using Fn = std::function<void(const HttpRequestPtr&, Api::FitnessGoalsController::Callback&&)>;

    HttpResponsePtr call(const char* who, HttpMethod method, const json& body, const Fn& fn) {
        auto req = TestHelpers::authed_json(plain(who), body, method);
        HttpResponsePtr captured;
        fn(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    json get(const char* who) {
        auto r = call(who, Get, json::object(), [&](auto req, auto cb) { c.get(req, std::move(cb)); });
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        TestHelpers::expect_2xx_matches_schema(r, "GET", "/api/v1/fitness/goals");
        return body_of(r)["data"];
    }

    HttpResponsePtr put(const char* who, const json& body) {
        auto r = call(who, Put, body, [&](auto req, auto cb) { c.put(req, std::move(cb)); });
        TestHelpers::expect_2xx_matches_schema(r, "PUT", "/api/v1/fitness/goals");
        return r;
    }
};

class FitnessGoalsOffTest : public FitnessGoalsApiTest {
protected:
    std::string config_file_name() const override { return "fitness_goals_off_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["fitness"]["enabled"] = false; }
};

}  // namespace

TEST_F(FitnessGoalsApiTest, DefaultWhileUnsetThenSetAndCleared) {
    const json unset = get(kAnna);
    EXPECT_EQ(unset["steps"], 8000);
    EXPECT_EQ(unset["steps_set"], false);

    auto r = put(kAnna, {{"steps", 12000}});
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    EXPECT_EQ(body_of(r)["data"]["steps"], 12000);
    EXPECT_EQ(body_of(r)["data"]["steps_set"], true);
    EXPECT_EQ(get(kAnna)["steps"], 12000);
    EXPECT_EQ(get(kBoris)["steps"], 8000) << "Anna's goal is hers";

    r = put(kAnna, {{"steps", nullptr}});
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    EXPECT_EQ(body_of(r)["data"]["steps"], 8000);
    EXPECT_EQ(body_of(r)["data"]["steps_set"], false);
}

TEST_F(FitnessGoalsApiTest, StepsOutOfRangeIsRefused) {
    for (const json& body : {json::object(),
                             json{{"steps", 999}},
                             json{{"steps", 100001}},
                             json{{"steps", 8000.5}},
                             json{{"steps", "8000"}}}) {
        auto r = put(kAnna, body);
        EXPECT_EQ(r->statusCode(), k400BadRequest) << body.dump();
        EXPECT_EQ(body_of(r)["error"], "invalid_steps") << std::string(r->body());
    }
    EXPECT_EQ(put(kAnna, {{"steps", 1000}})->statusCode(), k200OK);
    EXPECT_EQ(put(kAnna, {{"steps", 100000}})->statusCode(), k200OK);
    EXPECT_EQ(get(kAnna)["steps"], 100000);
}

TEST_F(FitnessGoalsOffTest, RoutesAre404WhileTheModuleIsOff) {
    auto r = call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.get(req, std::move(cb)); });
    EXPECT_EQ(r->statusCode(), k404NotFound);
    r = call(kAnna, Put, json{{"steps", 9000}}, [&](auto req, auto cb) { c.put(req, std::move(cb)); });
    EXPECT_EQ(r->statusCode(), k404NotFound);
}
