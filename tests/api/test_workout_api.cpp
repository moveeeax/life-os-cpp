/**
 * @file test_workout_api.cpp
 * @brief Workout module routes: exercise library, custom exercises, routines,
 *        guards and the disabled module.
 */

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/WorkoutController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kOwner = "11111111-1111-4111-8111-111111111111";
constexpr const char* kStranger = "99999999-9999-4999-8999-999999999999";
constexpr const char* kRoutineId = "22222222-2222-4222-8222-222222222222";

Security::Auth::AuthPrincipal principal(const std::string& subject, std::uint32_t permissions) {
    Security::Auth::AuthPrincipal p;
    p.subject = subject;
    p.raw_claims = json{{"sub", p.subject}, {"permissions", permissions}};
    return p;
}

Security::Auth::AuthPrincipal owner() {
    return principal(kOwner, Domain::Permission::kGeneral | Domain::Permission::kFitnessRead);
}

Security::Auth::AuthPrincipal admin() {
    return principal(kOwner, Domain::Permission::kAdminister);
}

Security::Auth::AuthPrincipal stranger() {
    return principal(kStranger, Domain::Permission::kGeneral | Domain::Permission::kFitnessRead);
}

json body_of(const HttpResponsePtr& resp) {
    return json::parse(std::string(resp->body()));
}

class WorkoutApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::WorkoutController controller;

    std::string config_file_name() const override { return "workout_api_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override { cfg["workout"]["enabled"] = true; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        // The library rows come from the seed migration and stay; only what
        // the tests create is cleared.
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE routines CASCADE");
            txn.exec("DELETE FROM exercises WHERE source = 'custom'");
            txn.exec("UPDATE exercises SET archived = false, tracking_mode = 'weight_reps' WHERE id = 'Barbell_Squat'");
            return true;
        });
    }

    HttpResponsePtr list_exercises(const Security::Auth::AuthPrincipal& p,
                                   const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto request = TestHelpers::authed(p, Get);
        for (const auto& [k, v] : params) {
            request->setParameter(k, v);
        }
        HttpResponsePtr captured;
        controller.listExercises(request, [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    HttpResponsePtr create_exercise(const Security::Auth::AuthPrincipal& p, const json& body) {
        HttpResponsePtr captured;
        controller.createExercise(TestHelpers::authed_json(p, body), [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    HttpResponsePtr get_exercise(const Security::Auth::AuthPrincipal& p, const std::string& id) {
        HttpResponsePtr captured;
        controller.getExercise(
            TestHelpers::authed(p, Get), [&](const HttpResponsePtr& r) { captured = r; }, id);
        return captured;
    }

    HttpResponsePtr patch_exercise(const Security::Auth::AuthPrincipal& p, const std::string& id, const json& body) {
        HttpResponsePtr captured;
        controller.updateExercise(
            TestHelpers::authed_json(p, body, Patch), [&](const HttpResponsePtr& r) { captured = r; }, id);
        return captured;
    }

    HttpResponsePtr put_routine(const Security::Auth::AuthPrincipal& p, const std::string& id, const json& body) {
        HttpResponsePtr captured;
        controller.putRoutine(
            TestHelpers::authed_json(p, body, Put), [&](const HttpResponsePtr& r) { captured = r; }, id);
        return captured;
    }

    HttpResponsePtr get_routine(const Security::Auth::AuthPrincipal& p, const std::string& id) {
        HttpResponsePtr captured;
        controller.getRoutine(
            TestHelpers::authed(p, Get), [&](const HttpResponsePtr& r) { captured = r; }, id);
        return captured;
    }

    HttpResponsePtr list_routines(const Security::Auth::AuthPrincipal& p) {
        HttpResponsePtr captured;
        controller.listRoutines(TestHelpers::authed(p, Get), [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    HttpResponsePtr delete_routine(const Security::Auth::AuthPrincipal& p, const std::string& id) {
        HttpResponsePtr captured;
        controller.deleteRoutine(
            TestHelpers::authed(p, Delete), [&](const HttpResponsePtr& r) { captured = r; }, id);
        return captured;
    }

    static json pull_day() {
        return json{{"name", "Pull day"},
                    {"weekday", 1},
                    {"exercises",
                     json::array({json{{"exercise_id", "Barbell_Squat"},
                                       {"target_sets", 4},
                                       {"target_reps_min", 6},
                                       {"target_reps_max", 10},
                                       {"rest_seconds", 120}},
                                  json{{"exercise_id", "Barbell_Deadlift"}}})}};
    }
};

}  // namespace

// ── exercises ────────────────────────────────────────────────────────────────

TEST_F(WorkoutApiTest, LibraryIsSeededAndPaged) {
    const auto resp = list_exercises(owner(), {{"limit", "5"}});
    ASSERT_TRUE(resp);
    ASSERT_EQ(resp->statusCode(), k200OK) << resp->body();
    const auto body = body_of(resp);
    ASSERT_TRUE(body.contains("data") && body.contains("total") && body.contains("count")) << body.dump();
    EXPECT_EQ(body["count"], 5);
    EXPECT_EQ(body["data"].size(), 5u);
    EXPECT_EQ(body["total"], 876);
    const auto& first = body["data"][0];
    for (const char* key : {"id",
                            "source",
                            "name",
                            "category",
                            "primary_muscles",
                            "instructions",
                            "images",
                            "tracking_mode",
                            "archived"}) {
        EXPECT_TRUE(first.contains(key)) << key;
    }
    EXPECT_EQ(first["source"], "library");
}

TEST_F(WorkoutApiTest, SearchFiltersByNameMuscleAndEquipment) {
    const auto by_name = body_of(list_exercises(owner(), {{"q", "squat"}, {"equipment", "barbell"}, {"limit", "200"}}));
    ASSERT_TRUE(by_name.contains("data")) << by_name.dump();
    EXPECT_GT(by_name["total"].get<long>(), 0);
    for (const auto& e : by_name["data"]) {
        EXPECT_EQ(e["equipment"], "barbell");
    }

    const auto by_muscle = body_of(list_exercises(owner(), {{"muscle", "lats"}, {"limit", "1"}}));
    ASSERT_TRUE(by_muscle.contains("total")) << by_muscle.dump();
    EXPECT_GT(by_muscle["total"].get<long>(), 0);
    EXPECT_LT(by_muscle["total"].get<long>(), 876);

    const auto none = body_of(list_exercises(owner(), {{"q", "no-such-exercise-zzz"}}));
    EXPECT_EQ(none["total"], 0);
    EXPECT_TRUE(none["data"].empty());
}

TEST_F(WorkoutApiTest, UnknownSourceIs400) {
    const auto resp = list_exercises(owner(), {{"source", "elsewhere"}});
    ASSERT_TRUE(resp);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(resp)["error"], "invalid_source");
}

TEST_F(WorkoutApiTest, CustomExerciseIsCreatedListedAndPrivate) {
    const auto created = create_exercise(owner(),
                                         json{{"name", "Garage pulley row"},
                                              {"tracking_mode", "weight_reps"},
                                              {"equipment", "cable"},
                                              {"primary_muscles", json::array({"lats", "biceps"})},
                                              {"instructions", json::array({"Pull.", "Release."})}});
    ASSERT_TRUE(created);
    ASSERT_EQ(created->statusCode(), k201Created) << created->body();
    const auto exercise = body_of(created)["data"];
    const std::string id = exercise["id"].get<std::string>();
    EXPECT_EQ(id.rfind("custom_", 0), 0u);
    EXPECT_EQ(exercise["source"], "custom");
    EXPECT_EQ(exercise["primary_muscles"], json::array({"lats", "biceps"}));
    EXPECT_EQ(exercise["instructions"].size(), 2u);

    const auto mine = body_of(list_exercises(owner(), {{"source", "custom"}}));
    EXPECT_EQ(mine["total"], 1);

    // Another user neither lists nor reads it.
    EXPECT_EQ(body_of(list_exercises(stranger(), {{"source", "custom"}}))["total"], 0);
    const auto foreign = get_exercise(stranger(), id);
    ASSERT_TRUE(foreign);
    EXPECT_EQ(foreign->statusCode(), k404NotFound);
}

TEST_F(WorkoutApiTest, CreateValidatesNameAndMode) {
    const auto no_name = create_exercise(owner(), json{{"tracking_mode", "weight_reps"}});
    ASSERT_TRUE(no_name);
    EXPECT_EQ(no_name->statusCode(), k400BadRequest);

    const auto bad_mode = create_exercise(owner(), json{{"name", "X"}, {"tracking_mode", "by_feel"}});
    ASSERT_TRUE(bad_mode);
    EXPECT_EQ(bad_mode->statusCode(), k400BadRequest);

    const auto bad_list =
        create_exercise(owner(), json{{"name", "X"}, {"tracking_mode", "weight_reps"}, {"primary_muscles", "lats"}});
    ASSERT_TRUE(bad_list);
    EXPECT_EQ(bad_list->statusCode(), k400BadRequest);
}

TEST_F(WorkoutApiTest, LibraryExerciseIsReadOnlyForANonAdmin) {
    // The library is shared by every user: fitness:read alone must not change it.
    const auto resp = patch_exercise(owner(), "Barbell_Squat", json{{"tracking_mode", "bodyweight_reps"}});
    ASSERT_TRUE(resp);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(resp)["error"], "library_exercise_read_only");
    EXPECT_EQ(body_of(get_exercise(owner(), "Barbell_Squat"))["data"]["tracking_mode"], "weight_reps");
}

TEST_F(WorkoutApiTest, AdminChangesModeAndArchiveOfALibraryExerciseButNotContent) {
    const auto mode = patch_exercise(admin(), "Barbell_Squat", json{{"tracking_mode", "bodyweight_reps"}});
    ASSERT_TRUE(mode);
    ASSERT_EQ(mode->statusCode(), k200OK) << mode->body();
    EXPECT_EQ(body_of(mode)["data"]["tracking_mode"], "bodyweight_reps");

    const auto rename = patch_exercise(admin(), "Barbell_Squat", json{{"name", "My squat"}});
    ASSERT_TRUE(rename);
    EXPECT_EQ(rename->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(rename)["error"], "library_exercise_read_only");

    // Archived exercises drop out of the default list and come back on request.
    ASSERT_EQ(patch_exercise(admin(), "Barbell_Squat", json{{"archived", true}})->statusCode(), k200OK);
    EXPECT_EQ(body_of(list_exercises(owner(), {{"q", "Barbell Squat"}, {"equipment", "barbell"}}))["total"],
              body_of(list_exercises(owner(),
                                     {{"q", "Barbell Squat"}, {"equipment", "barbell"}, {"archived", "true"}}))["total"]
                      .get<long>() -
                  1);
}

TEST_F(WorkoutApiTest, PatchOfMissingExerciseIs404) {
    const auto resp = patch_exercise(owner(), "No_Such_Exercise", json{{"archived", true}});
    ASSERT_TRUE(resp);
    EXPECT_EQ(resp->statusCode(), k404NotFound);
}

// ── routines ─────────────────────────────────────────────────────────────────

TEST_F(WorkoutApiTest, RoutineIsStoredWithExercisesInOrderAndDefaults) {
    const auto put = put_routine(owner(), kRoutineId, pull_day());
    ASSERT_TRUE(put);
    ASSERT_EQ(put->statusCode(), k200OK) << put->body();
    const auto routine = body_of(put)["data"];
    EXPECT_EQ(routine["id"], kRoutineId);
    EXPECT_EQ(routine["weekday"], 1);
    ASSERT_EQ(routine["exercises"].size(), 2u);
    EXPECT_EQ(routine["exercises"][0]["exercise_id"], "Barbell_Squat");
    EXPECT_EQ(routine["exercises"][0]["position"], 1);
    EXPECT_EQ(routine["exercises"][0]["target_sets"], 4);
    EXPECT_EQ(routine["exercises"][0]["rest_seconds"], 120);
    EXPECT_EQ(routine["exercises"][0]["exercise_name"], "Barbell Squat");
    // Second item sent only the exercise: defaults apply, targets stay empty.
    EXPECT_EQ(routine["exercises"][1]["target_sets"], 3);
    EXPECT_EQ(routine["exercises"][1]["rest_seconds"], 90);
    EXPECT_TRUE(routine["exercises"][1]["target_reps_min"].is_null());

    const auto listed = body_of(list_routines(owner()));
    ASSERT_EQ(listed["data"].size(), 1u);
    EXPECT_EQ(listed["data"][0]["exercise_count"], 2);
}

TEST_F(WorkoutApiTest, PutReplacesTheExerciseList) {
    ASSERT_EQ(put_routine(owner(), kRoutineId, pull_day())->statusCode(), k200OK);
    const auto replaced = put_routine(owner(),
                                      kRoutineId,
                                      json{{"name", "Legs"},
                                           {"weekday", nullptr},
                                           {"exercises", json::array({json{{"exercise_id", "Barbell_Deadlift"}}})}});
    ASSERT_TRUE(replaced);
    ASSERT_EQ(replaced->statusCode(), k200OK) << replaced->body();
    const auto routine = body_of(replaced)["data"];
    EXPECT_EQ(routine["name"], "Legs");
    EXPECT_TRUE(routine["weekday"].is_null());
    ASSERT_EQ(routine["exercises"].size(), 1u);
    EXPECT_EQ(routine["exercises"][0]["exercise_id"], "Barbell_Deadlift");
    EXPECT_EQ(body_of(list_routines(owner()))["data"].size(), 1u);
}

TEST_F(WorkoutApiTest, PutValidatesBodyAndExerciseIds) {
    const auto unknown =
        put_routine(owner(),
                    kRoutineId,
                    json{{"name", "A"}, {"exercises", json::array({json{{"exercise_id", "No_Such_Exercise"}}})}});
    ASSERT_TRUE(unknown);
    EXPECT_EQ(unknown->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(unknown)["error"], "unknown_exercise");
    // Nothing was stored by the rejected request.
    EXPECT_TRUE(body_of(list_routines(owner()))["data"].empty());

    const auto bad_weekday =
        put_routine(owner(), kRoutineId, json{{"name", "A"}, {"weekday", 9}, {"exercises", json::array()}});
    ASSERT_TRUE(bad_weekday);
    EXPECT_EQ(bad_weekday->statusCode(), k400BadRequest);

    const auto bad_range = put_routine(
        owner(),
        kRoutineId,
        json{{"name", "A"},
             {"exercises",
              json::array({json{{"exercise_id", "Barbell_Squat"}, {"target_reps_min", 12}, {"target_reps_max", 8}}})}});
    ASSERT_TRUE(bad_range);
    EXPECT_EQ(bad_range->statusCode(), k400BadRequest);

    const auto no_exercises = put_routine(owner(), kRoutineId, json{{"name", "A"}});
    ASSERT_TRUE(no_exercises);
    EXPECT_EQ(no_exercises->statusCode(), k400BadRequest);

    const auto bad_id = put_routine(owner(), "not-a-uuid", pull_day());
    ASSERT_TRUE(bad_id);
    EXPECT_EQ(bad_id->statusCode(), k400BadRequest);
}

TEST_F(WorkoutApiTest, RoutineIsInvisibleAndImmutableForAnotherUser) {
    ASSERT_EQ(put_routine(owner(), kRoutineId, pull_day())->statusCode(), k200OK);

    EXPECT_EQ(get_routine(stranger(), kRoutineId)->statusCode(), k404NotFound);
    EXPECT_TRUE(body_of(list_routines(stranger()))["data"].empty());
    EXPECT_EQ(put_routine(stranger(), kRoutineId, pull_day())->statusCode(), k404NotFound);
    EXPECT_EQ(delete_routine(stranger(), kRoutineId)->statusCode(), k404NotFound);

    // The owner still has it, unchanged.
    EXPECT_EQ(body_of(get_routine(owner(), kRoutineId))["data"]["name"], "Pull day");
}

TEST_F(WorkoutApiTest, DeleteRemovesTheRoutine) {
    ASSERT_EQ(put_routine(owner(), kRoutineId, pull_day())->statusCode(), k200OK);
    EXPECT_EQ(delete_routine(owner(), kRoutineId)->statusCode(), k200OK);
    EXPECT_EQ(get_routine(owner(), kRoutineId)->statusCode(), k404NotFound);
    EXPECT_EQ(delete_routine(owner(), kRoutineId)->statusCode(), k404NotFound);
}

// ── guards ───────────────────────────────────────────────────────────────────

TEST_F(WorkoutApiTest, NeedsFitnessReadAndAUserAccount) {
    const auto plain = list_exercises(principal(kOwner, Domain::Permission::kGeneral));
    ASSERT_TRUE(plain);
    EXPECT_EQ(plain->statusCode(), k403Forbidden);

    // A principal whose subject is not a user id (static bearer) has no rows to own.
    const auto no_user =
        list_exercises(principal("static-bearer", Domain::Permission::kGeneral | Domain::Permission::kFitnessRead));
    ASSERT_TRUE(no_user);
    EXPECT_EQ(no_user->statusCode(), k403Forbidden);
    EXPECT_EQ(body_of(no_user)["error"], "no_user_account");

    HttpResponsePtr anonymous;
    controller.listExercises(TestHelpers::make_request(Get), [&](const HttpResponsePtr& r) { anonymous = r; });
    ASSERT_TRUE(anonymous);
    EXPECT_NE(anonymous->statusCode(), k200OK);
}

namespace {

class WorkoutDisabledTest : public TestHelpers::CoreBackedTest {
protected:
    Api::WorkoutController controller;

    std::string config_file_name() const override { return "workout_disabled_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override { cfg["workout"]["enabled"] = false; }
};

}  // namespace

TEST_F(WorkoutDisabledTest, EveryRouteIs404WhileTheModuleIsOff) {
    HttpResponsePtr list;
    controller.listExercises(TestHelpers::authed(owner(), Get), [&](const HttpResponsePtr& r) { list = r; });
    ASSERT_TRUE(list);
    EXPECT_EQ(list->statusCode(), k404NotFound);

    HttpResponsePtr routines;
    controller.listRoutines(TestHelpers::authed(owner(), Get), [&](const HttpResponsePtr& r) { routines = r; });
    ASSERT_TRUE(routines);
    EXPECT_EQ(routines->statusCode(), k404NotFound);
}
