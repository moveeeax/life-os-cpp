/**
 * @file test_workout_api.cpp
 * @brief Workout module routes: exercise library, custom exercises, routines,
 *        sessions with sets, the links to Mi Fitness data, guards and the
 *        disabled module.
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

// ── sessions ─────────────────────────────────────────────────────────────────

namespace {

constexpr const char* kSetId = "33333333-3333-4333-8333-333333333333";
constexpr const char* kOtherSetId = "44444444-4444-4444-8444-444444444444";
constexpr const char* kMissingId = "55555555-5555-4555-8555-555555555555";

class WorkoutSessionsTest : public WorkoutApiTest {
protected:
    void SetUp() override {
        WorkoutApiTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE workout_sessions CASCADE");
            txn.exec("TRUNCATE TABLE heart_rate_samples, workouts, body_measurements");
            return true;
        });
    }

    static void sql(const std::string& statement) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec(statement);
            return true;
        });
    }

    static Security::Auth::AuthPrincipal syncer() {
        return principal(
            kOwner, Domain::Permission::kGeneral | Domain::Permission::kFitnessRead | Domain::Permission::kFitnessSync);
    }

    HttpResponsePtr start(const Security::Auth::AuthPrincipal& p, const json& body = json::object()) {
        HttpResponsePtr captured;
        controller.startSession(TestHelpers::authed_json(p, body), [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    HttpResponsePtr active(const Security::Auth::AuthPrincipal& p) {
        HttpResponsePtr captured;
        controller.activeSession(TestHelpers::authed(p, Get), [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    HttpResponsePtr history(const Security::Auth::AuthPrincipal& p) {
        HttpResponsePtr captured;
        controller.listSessions(TestHelpers::authed(p, Get), [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    HttpResponsePtr get_session(const Security::Auth::AuthPrincipal& p, const std::string& id) {
        HttpResponsePtr captured;
        controller.getSession(
            TestHelpers::authed(p, Get), [&](const HttpResponsePtr& r) { captured = r; }, id);
        return captured;
    }

    HttpResponsePtr patch_session(const Security::Auth::AuthPrincipal& p, const std::string& id, const json& body) {
        HttpResponsePtr captured;
        controller.patchSession(
            TestHelpers::authed_json(p, body, Patch), [&](const HttpResponsePtr& r) { captured = r; }, id);
        return captured;
    }

    HttpResponsePtr delete_session(const Security::Auth::AuthPrincipal& p, const std::string& id) {
        HttpResponsePtr captured;
        controller.deleteSession(
            TestHelpers::authed(p, Delete), [&](const HttpResponsePtr& r) { captured = r; }, id);
        return captured;
    }

    HttpResponsePtr add_exercise(const Security::Auth::AuthPrincipal& p, const std::string& id, const json& body) {
        HttpResponsePtr captured;
        controller.addSessionExercise(
            TestHelpers::authed_json(p, body), [&](const HttpResponsePtr& r) { captured = r; }, id);
        return captured;
    }

    HttpResponsePtr remove_exercise(const Security::Auth::AuthPrincipal& p,
                                    const std::string& id,
                                    const std::string& eid) {
        HttpResponsePtr captured;
        controller.removeSessionExercise(
            TestHelpers::authed(p, Delete), [&](const HttpResponsePtr& r) { captured = r; }, id, eid);
        return captured;
    }

    HttpResponsePtr put_set(const Security::Auth::AuthPrincipal& p, const std::string& id, const json& body) {
        HttpResponsePtr captured;
        controller.putSet(
            TestHelpers::authed_json(p, body, Put), [&](const HttpResponsePtr& r) { captured = r; }, id);
        return captured;
    }

    HttpResponsePtr delete_set(const Security::Auth::AuthPrincipal& p, const std::string& id) {
        HttpResponsePtr captured;
        controller.deleteSet(
            TestHelpers::authed(p, Delete), [&](const HttpResponsePtr& r) { captured = r; }, id);
        return captured;
    }

    HttpResponsePtr heart_rate(const Security::Auth::AuthPrincipal& p, const std::string& id) {
        HttpResponsePtr captured;
        controller.sessionHeartRate(
            TestHelpers::authed(p, Get), [&](const HttpResponsePtr& r) { captured = r; }, id);
        return captured;
    }

    HttpResponsePtr reconcile(const Security::Auth::AuthPrincipal& p, const json& body) {
        HttpResponsePtr captured;
        controller.reconcile(TestHelpers::authed_json(p, body), [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    /// An empty session with one exercise; returns the session as stored.
    json session_with(const std::string& exercise_id) {
        const auto started = start(owner());
        EXPECT_EQ(started->statusCode(), k201Created);
        const std::string id = body_of(started)["data"]["id"].get<std::string>();
        const auto added = add_exercise(owner(), id, json{{"exercise_id", exercise_id}});
        EXPECT_EQ(added->statusCode(), k201Created);
        return body_of(added)["data"];
    }

    static json set_of(const std::string& session_exercise_id, int position, double weight, int reps) {
        return json{{"session_exercise_id", session_exercise_id},
                    {"position", position},
                    {"weight_kg", weight},
                    {"reps", reps}};
    }
};

}  // namespace

TEST_F(WorkoutSessionsTest, StartFromRoutineCopiesExercisesTargetsAndBodyWeight) {
    sql("INSERT INTO body_measurements (user_id, timestamp, weight_kg) VALUES ('t', now() - interval '1 day', 80.5)");
    ASSERT_EQ(put_routine(owner(), kRoutineId, pull_day())->statusCode(), k200OK);

    const auto resp = start(owner(), json{{"routine_id", kRoutineId}});
    ASSERT_TRUE(resp);
    ASSERT_EQ(resp->statusCode(), k201Created);
    const json s = body_of(resp)["data"];
    EXPECT_EQ(s["name"], "Pull day");
    EXPECT_EQ(s["routine_id"], kRoutineId);
    EXPECT_TRUE(s["finished_at"].is_null());
    EXPECT_TRUE(s["health_status"].is_null());
    EXPECT_DOUBLE_EQ(s["bodyweight_kg"].get<double>(), 80.5);
    ASSERT_EQ(s["exercises"].size(), 2u);
    EXPECT_EQ(s["exercises"][0]["exercise_id"], "Barbell_Squat");
    EXPECT_EQ(s["exercises"][0]["target_sets"], 4);
    EXPECT_EQ(s["exercises"][0]["rest_seconds"], 120);
    EXPECT_EQ(s["exercises"][1]["exercise_id"], "Barbell_Deadlift");
    EXPECT_TRUE(s["exercises"][0]["sets"].empty());
}

TEST_F(WorkoutSessionsTest, OnlyOneSessionIsActiveAtATime) {
    const auto none = active(owner());
    ASSERT_EQ(none->statusCode(), k200OK);
    EXPECT_TRUE(body_of(none)["data"].is_null());

    const auto first = start(owner());
    ASSERT_EQ(first->statusCode(), k201Created);
    const auto second = start(owner());
    ASSERT_EQ(second->statusCode(), k409Conflict);
    EXPECT_EQ(body_of(second)["error"], "session_active");

    EXPECT_EQ(body_of(active(owner()))["data"]["id"], body_of(first)["data"]["id"]);
    // Another user has no active session and may start one.
    EXPECT_TRUE(body_of(active(stranger()))["data"].is_null());
    EXPECT_EQ(start(stranger())->statusCode(), k201Created);
}

TEST_F(WorkoutSessionsTest, StartValidatesTheRoutine) {
    EXPECT_EQ(start(owner(), json{{"routine_id", "nope"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(start(owner(), json{{"routine_id", kMissingId}})->statusCode(), k404NotFound);
}

TEST_F(WorkoutSessionsTest, ExercisesAreAddedAndDropped) {
    const json s = session_with("Barbell_Squat");
    const std::string id = s["id"].get<std::string>();
    ASSERT_EQ(s["exercises"].size(), 1u);
    const std::string eid = s["exercises"][0]["id"].get<std::string>();

    EXPECT_EQ(add_exercise(owner(), id, json{{"exercise_id", "No_Such_Exercise"}})->statusCode(), k404NotFound);
    EXPECT_EQ(add_exercise(owner(), id, json::object())->statusCode(), k400BadRequest);
    EXPECT_EQ(add_exercise(stranger(), id, json{{"exercise_id", "Barbell_Squat"}})->statusCode(), k404NotFound);

    const auto second = add_exercise(owner(), id, json{{"exercise_id", "Barbell_Deadlift"}});
    ASSERT_EQ(second->statusCode(), k201Created);
    ASSERT_EQ(body_of(second)["data"]["exercises"].size(), 2u);
    EXPECT_EQ(body_of(second)["data"]["exercises"][1]["position"], 2);

    EXPECT_EQ(remove_exercise(stranger(), id, eid)->statusCode(), k404NotFound);
    const auto dropped = remove_exercise(owner(), id, eid);
    ASSERT_EQ(dropped->statusCode(), k200OK);
    ASSERT_EQ(body_of(dropped)["data"]["exercises"].size(), 1u);
    EXPECT_EQ(body_of(dropped)["data"]["exercises"][0]["exercise_id"], "Barbell_Deadlift");
    EXPECT_EQ(remove_exercise(owner(), id, eid)->statusCode(), k404NotFound);
}

TEST_F(WorkoutSessionsTest, ResentSetReplacesItselfInsteadOfDuplicating) {
    const json s = session_with("Barbell_Squat");
    const std::string id = s["id"].get<std::string>();
    const std::string eid = s["exercises"][0]["id"].get<std::string>();

    const auto first = put_set(owner(), kSetId, set_of(eid, 1, 100, 5));
    ASSERT_EQ(first->statusCode(), k200OK);
    EXPECT_EQ(body_of(first)["data"]["kind"], "work");
    EXPECT_EQ(body_of(first)["data"]["reps"], 5);

    json again = set_of(eid, 1, 102.5, 6);
    again["rpe"] = 8.5;
    again["kind"] = "warmup";
    ASSERT_EQ(put_set(owner(), kSetId, again)->statusCode(), k200OK);

    const json stored = body_of(get_session(owner(), id))["data"]["exercises"][0]["sets"];
    ASSERT_EQ(stored.size(), 1u);
    EXPECT_EQ(stored[0]["id"], kSetId);
    EXPECT_DOUBLE_EQ(stored[0]["weight_kg"].get<double>(), 102.5);
    EXPECT_EQ(stored[0]["reps"], 6);
    EXPECT_EQ(stored[0]["kind"], "warmup");
    EXPECT_DOUBLE_EQ(stored[0]["rpe"].get<double>(), 8.5);

    EXPECT_EQ(delete_set(stranger(), kSetId)->statusCode(), k404NotFound);
    EXPECT_EQ(delete_set(owner(), kSetId)->statusCode(), k200OK);
    EXPECT_EQ(delete_set(owner(), kSetId)->statusCode(), k404NotFound);
}

TEST_F(WorkoutSessionsTest, SetIsValidatedAndBoundToTheOwnersSession) {
    const json s = session_with("Barbell_Squat");
    const std::string eid = s["exercises"][0]["id"].get<std::string>();

    EXPECT_EQ(put_set(owner(), "not-a-uuid", set_of(eid, 1, 100, 5))->statusCode(), k400BadRequest);
    EXPECT_EQ(put_set(owner(), kSetId, set_of(eid, 1, 100, -1))->statusCode(), k400BadRequest);
    EXPECT_EQ(put_set(owner(), kSetId, set_of(eid, 1, 5000, 5))->statusCode(), k400BadRequest);
    EXPECT_EQ(put_set(owner(), kSetId, set_of(eid, 0, 100, 5))->statusCode(), k400BadRequest);
    EXPECT_EQ(put_set(owner(), kSetId, json{{"session_exercise_id", eid}})->statusCode(), k400BadRequest);
    EXPECT_EQ(put_set(owner(), kSetId, json{{"position", 1}})->statusCode(), k400BadRequest);

    json bad = set_of(eid, 1, 100, 5);
    bad["rpe"] = 11;
    EXPECT_EQ(put_set(owner(), kSetId, bad)->statusCode(), k400BadRequest);
    bad = set_of(eid, 1, 100, 5);
    bad["kind"] = "drop";
    EXPECT_EQ(put_set(owner(), kSetId, bad)->statusCode(), k400BadRequest);
    bad = set_of(eid, 1, 100, 5);
    bad["completed_at"] = "yesterday";
    EXPECT_EQ(put_set(owner(), kSetId, bad)->statusCode(), k400BadRequest);
    bad["completed_at"] = "2026-13-40T09:00:00Z";
    const auto impossible = put_set(owner(), kSetId, bad);
    EXPECT_EQ(impossible->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(impossible)["error"], "invalid_timestamp");

    // Someone else's session exercise, and one that does not exist.
    EXPECT_EQ(put_set(stranger(), kSetId, set_of(eid, 1, 100, 5))->statusCode(), k404NotFound);
    EXPECT_EQ(put_set(owner(), kSetId, set_of(kMissingId, 1, 100, 5))->statusCode(), k404NotFound);
}

TEST_F(WorkoutSessionsTest, FinishedSessionLandsInHistoryWithVolumeOfWorkSets) {
    const json s = session_with("Barbell_Squat");
    const std::string id = s["id"].get<std::string>();
    const std::string eid = s["exercises"][0]["id"].get<std::string>();
    json warmup = set_of(eid, 1, 50, 5);
    warmup["kind"] = "warmup";
    ASSERT_EQ(put_set(owner(), kOtherSetId, warmup)->statusCode(), k200OK);
    ASSERT_EQ(put_set(owner(), kSetId, set_of(eid, 2, 100, 5))->statusCode(), k200OK);

    // An unfinished session is not history yet.
    EXPECT_EQ(body_of(history(owner()))["total"], 0);

    const auto finished = patch_session(owner(), id, json{{"finish", true}, {"name", "Legs"}, {"note", "easy"}});
    ASSERT_EQ(finished->statusCode(), k200OK);
    EXPECT_FALSE(body_of(finished)["data"]["finished_at"].is_null());
    EXPECT_EQ(body_of(finished)["data"]["name"], "Legs");
    // No heart-rate sample newer than the end: the band has not synced yet.
    EXPECT_EQ(body_of(finished)["data"]["health_status"], "pending");
    EXPECT_TRUE(body_of(active(owner()))["data"].is_null());

    const json page = body_of(history(owner()));
    ASSERT_EQ(page["total"], 1);
    ASSERT_EQ(page["data"].size(), 1u);
    EXPECT_EQ(page["data"][0]["id"], id);
    EXPECT_EQ(page["data"][0]["exercise_count"], 1);
    EXPECT_EQ(page["data"][0]["set_count"], 1);
    EXPECT_DOUBLE_EQ(page["data"][0]["volume_kg"].get<double>(), 500.0);
    EXPECT_EQ(body_of(history(stranger()))["total"], 0);
}

TEST_F(WorkoutSessionsTest, NextSessionShowsTheSetsOfThePreviousOne) {
    const json first = session_with("Barbell_Squat");
    ASSERT_EQ(put_set(owner(), kSetId, set_of(first["exercises"][0]["id"].get<std::string>(), 1, 100, 5))->statusCode(),
              k200OK);
    ASSERT_EQ(patch_session(owner(), first["id"].get<std::string>(), json{{"finish", true}})->statusCode(), k200OK);

    const json second = session_with("Barbell_Squat");
    const json previous = second["exercises"][0]["previous_sets"];
    ASSERT_EQ(previous.size(), 1u);
    EXPECT_DOUBLE_EQ(previous[0]["weight_kg"].get<double>(), 100.0);
    EXPECT_EQ(previous[0]["reps"], 5);
    EXPECT_TRUE(second["exercises"][0]["sets"].empty());
}

TEST_F(WorkoutSessionsTest, PatchValidatesTimes) {
    const std::string id = body_of(start(owner()))["data"]["id"].get<std::string>();

    EXPECT_EQ(patch_session(owner(), id, json{{"started_at", "yesterday"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(patch_session(owner(), id, json{{"finish", "yes"}})->statusCode(), k400BadRequest);
    const auto impossible = patch_session(owner(), id, json{{"started_at", "2026-13-40T09:00:00Z"}});
    EXPECT_EQ(impossible->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(impossible)["error"], "invalid_timestamp");
    const auto reversed = patch_session(
        owner(), id, json{{"started_at", "2026-09-01T11:00:00Z"}, {"finished_at", "2026-09-01T10:00:00Z"}});
    EXPECT_EQ(reversed->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(reversed)["error"], "invalid_times");

    EXPECT_EQ(patch_session(owner(), "not-a-uuid", json{{"finish", true}})->statusCode(), k400BadRequest);
    EXPECT_EQ(patch_session(owner(), kMissingId, json{{"finish", true}})->statusCode(), k404NotFound);
}

TEST_F(WorkoutSessionsTest, SessionBelongsToItsOwnerAndIsDeletedWithItsSets) {
    const json s = session_with("Barbell_Squat");
    const std::string id = s["id"].get<std::string>();
    ASSERT_EQ(put_set(owner(), kSetId, set_of(s["exercises"][0]["id"].get<std::string>(), 1, 100, 5))->statusCode(),
              k200OK);

    EXPECT_EQ(get_session(stranger(), id)->statusCode(), k404NotFound);
    EXPECT_EQ(patch_session(stranger(), id, json{{"finish", true}})->statusCode(), k404NotFound);
    EXPECT_EQ(heart_rate(stranger(), id)->statusCode(), k404NotFound);
    EXPECT_EQ(delete_session(stranger(), id)->statusCode(), k404NotFound);

    EXPECT_EQ(delete_session(owner(), id)->statusCode(), k200OK);
    EXPECT_EQ(get_session(owner(), id)->statusCode(), k404NotFound);
    EXPECT_EQ(delete_set(owner(), kSetId)->statusCode(), k404NotFound);
}

// ── health links ─────────────────────────────────────────────────────────────

TEST_F(WorkoutSessionsTest, ReconciliationGoesFromPendingToNoDataToMatched) {
    const std::string id = body_of(start(owner()))["data"]["id"].get<std::string>();
    const json window = {{"from", "2026-09-01"}, {"to", "2026-09-01"}};

    // Moving the times of a session finishes it and reconciles at once.
    const auto moved = patch_session(
        owner(), id, json{{"started_at", "2026-09-01T10:00:00Z"}, {"finished_at", "2026-09-01T11:00:00Z"}});
    ASSERT_EQ(moved->statusCode(), k200OK);
    EXPECT_EQ(body_of(moved)["data"]["health_status"], "pending");
    EXPECT_EQ(body_of(moved)["data"]["hr_samples"], 0);

    // The band synced past the session's end and brought nothing inside it.
    sql("INSERT INTO heart_rate_samples (user_id, timestamp, bpm, sample_type) "
        "VALUES ('t', '2026-09-01T12:00:00Z', 60, 'passive')");
    const auto after_sync = reconcile(syncer(), window);
    ASSERT_EQ(after_sync->statusCode(), k200OK);
    EXPECT_EQ(body_of(after_sync)["data"]["reconciled"], 1);
    EXPECT_EQ(body_of(get_session(owner(), id))["data"]["health_status"], "no_data");

    // Samples inside the session: mean and maximum come from them.
    sql("INSERT INTO heart_rate_samples (user_id, timestamp, bpm, sample_type) VALUES "
        "('t', '2026-09-01T10:10:00Z', 100, 'active'), ('t', '2026-09-01T10:20:00Z', 140, 'active')");
    ASSERT_EQ(reconcile(syncer(), window)->statusCode(), k200OK);
    json s = body_of(get_session(owner(), id))["data"];
    EXPECT_EQ(s["health_status"], "matched");
    EXPECT_EQ(s["hr_samples"], 2);
    EXPECT_EQ(s["hr_avg"], 120);
    EXPECT_EQ(s["hr_max"], 140);
    EXPECT_TRUE(s["band_workout_id"].is_null());
    EXPECT_FALSE(s["reconciled_at"].is_null());

    // A band workout over most of the session: its own heart rate wins. One
    // that only touches the session's edge is not a match.
    sql("INSERT INTO workouts (user_id, workout_id, activity_type, start_at, end_at, duration_minutes, "
        "calories_kcal, avg_heart_rate_bpm, max_heart_rate_bpm) VALUES "
        "('t', 'band-edge', 'walking', '2026-09-01T10:50:00Z', '2026-09-01T12:00:00Z', 70, 90, 95, 110), "
        "('t', 'band-main', 'strength', '2026-09-01T10:05:00Z', '2026-09-01T10:55:00Z', 50, 300, 130, 150)");
    ASSERT_EQ(reconcile(syncer(), window)->statusCode(), k200OK);
    s = body_of(get_session(owner(), id))["data"];
    EXPECT_EQ(s["health_status"], "matched");
    EXPECT_EQ(s["band_workout_id"], "band-main");
    EXPECT_DOUBLE_EQ(s["band_calories_kcal"].get<double>(), 300.0);
    EXPECT_EQ(s["hr_avg"], 130);
    EXPECT_EQ(s["hr_max"], 150);
    EXPECT_EQ(s["hr_samples"], 2);

    // The same input gives the same row.
    ASSERT_EQ(reconcile(syncer(), window)->statusCode(), k200OK);
    json again = body_of(get_session(owner(), id))["data"];
    again.erase("reconciled_at");
    s.erase("reconciled_at");
    EXPECT_EQ(again, s);

    // The chart gets the samples inside the session and the newest sample time.
    const auto chart = heart_rate(owner(), id);
    ASSERT_EQ(chart->statusCode(), k200OK);
    ASSERT_EQ(body_of(chart)["data"]["samples"].size(), 2u);
    EXPECT_EQ(body_of(chart)["data"]["samples"][0]["bpm"], 100);
    EXPECT_FALSE(body_of(chart)["data"]["latest_sample_at"].is_null());

    // Moving the session away from the data clears the match.
    const auto away = patch_session(
        owner(), id, json{{"started_at", "2026-08-01T10:00:00Z"}, {"finished_at", "2026-08-01T11:00:00Z"}});
    ASSERT_EQ(away->statusCode(), k200OK);
    EXPECT_EQ(body_of(away)["data"]["health_status"], "no_data");
    EXPECT_EQ(body_of(away)["data"]["hr_samples"], 0);
    EXPECT_TRUE(body_of(away)["data"]["hr_avg"].is_null());
    EXPECT_TRUE(body_of(away)["data"]["band_workout_id"].is_null());
}

TEST_F(WorkoutSessionsTest, ReconcileNeedsFitnessSyncAndAValidRange) {
    const json window = {{"from", "2026-09-01"}, {"to", "2026-09-02"}};
    EXPECT_EQ(reconcile(owner(), window)->statusCode(), k403Forbidden);

    const auto empty = reconcile(syncer(), window);
    ASSERT_EQ(empty->statusCode(), k200OK);
    EXPECT_EQ(body_of(empty)["data"]["reconciled"], 0);

    EXPECT_EQ(reconcile(syncer(), json{{"from", "2026-09-02"}, {"to", "2026-09-01"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(reconcile(syncer(), json{{"from", "yesterday"}, {"to", "2026-09-01"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(reconcile(syncer(), json{{"from", "2026-09-01"}})->statusCode(), k400BadRequest);
    const auto impossible = reconcile(syncer(), json{{"from", "2026-02-30"}, {"to", "2026-03-01"}});
    EXPECT_EQ(impossible->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(impossible)["error"], "invalid_timestamp");
}

TEST_F(WorkoutSessionsTest, ReadinessReturnsNullsWithoutDataAndNumbersWithIt) {
    HttpResponsePtr bare;
    controller.readiness(TestHelpers::authed(owner(), Get), [&](const HttpResponsePtr& r) { bare = r; });
    ASSERT_TRUE(bare);
    ASSERT_EQ(bare->statusCode(), k200OK);
    const json none = body_of(bare)["data"];
    for (const char* key : {"sleep_minutes",
                            "sleep_score",
                            "sleep_minutes_avg_30d",
                            "resting_bpm",
                            "resting_bpm_avg_30d",
                            "stress",
                            "stress_avg_30d",
                            "bodyweight_kg"}) {
        EXPECT_TRUE(none.contains(key)) << key;
    }
    EXPECT_TRUE(none["resting_bpm"].is_null());
    EXPECT_TRUE(none["bodyweight_kg"].is_null());

    sql("INSERT INTO heart_rate_samples (user_id, timestamp, bpm, sample_type) VALUES "
        "('t', now() - interval '2 hours', 52, 'resting'), ('t', now() - interval '10 days', 58, 'resting')");
    sql("INSERT INTO body_measurements (user_id, timestamp, weight_kg) VALUES ('t', now() - interval '1 day', 80.5)");
    HttpResponsePtr filled;
    controller.readiness(TestHelpers::authed(owner(), Get), [&](const HttpResponsePtr& r) { filled = r; });
    ASSERT_EQ(filled->statusCode(), k200OK);
    const json some = body_of(filled)["data"];
    EXPECT_EQ(some["resting_bpm"], 52);
    EXPECT_EQ(some["resting_bpm_avg_30d"], 55);
    EXPECT_DOUBLE_EQ(some["bodyweight_kg"].get<double>(), 80.5);
}

TEST_F(WorkoutSessionsTest, SessionRoutesNeedFitnessRead) {
    EXPECT_EQ(start(principal(kOwner, Domain::Permission::kGeneral))->statusCode(), k403Forbidden);
    EXPECT_EQ(history(principal(kOwner, Domain::Permission::kGeneral))->statusCode(), k403Forbidden);
    const auto no_user =
        start(principal("static-bearer", Domain::Permission::kGeneral | Domain::Permission::kFitnessRead));
    EXPECT_EQ(no_user->statusCode(), k403Forbidden);
    EXPECT_EQ(body_of(no_user)["error"], "no_user_account");
}

TEST_F(WorkoutDisabledTest, SessionRoutesAre404WhileTheModuleIsOff) {
    HttpResponsePtr started;
    controller.startSession(TestHelpers::authed_json(owner(), json::object()),
                            [&](const HttpResponsePtr& r) { started = r; });
    ASSERT_TRUE(started);
    EXPECT_EQ(started->statusCode(), k404NotFound);

    HttpResponsePtr ready;
    controller.readiness(TestHelpers::authed(owner(), Get), [&](const HttpResponsePtr& r) { ready = r; });
    ASSERT_TRUE(ready);
    EXPECT_EQ(ready->statusCode(), k404NotFound);
}
