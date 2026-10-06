/**
 * @file test_food_api.cpp
 * @brief Food module routes: products, the diary, goals, Open Food Facts,
 *        two users apart, the module switch.
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

#include "api/FoodController.hpp"
#include "cache/Cache.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "food/Http.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa5";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb5";
constexpr const char* kMissingId = "55555555-5555-4555-8555-555555555555";

Security::Auth::AuthPrincipal user(const std::string& id) {
    Security::Auth::AuthPrincipal p;
    p.subject = id;
    p.raw_claims = json{{"sub", id}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

json body_of(const HttpResponsePtr& resp) {
    return json::parse(std::string(resp->body()));
}

/// Open Food Facts, scripted.
class ScriptedTransport : public Food::Http::Transport {
public:
    std::vector<Food::Http::Response> replies;
    std::vector<std::string> urls;
    bool fail = false;

    Food::Http::Response get(const std::string& url, const std::vector<std::pair<std::string, std::string>>&) override {
        urls.push_back(url);
        if (fail) {
            throw Food::Http::TransportError("Could not resolve host");
        }
        if (replies.empty()) {
            throw std::runtime_error("ScriptedTransport: unexpected request to " + url);
        }
        auto r = replies.front();
        replies.erase(replies.begin());
        return r;
    }
};

const char* const kProductAnswer =
    R"({"status":1,"code":"3017624010701","product":{"code":"3017624010701","product_name":"Nutella","brands":"Ferrero",)"
    R"("serving_size":"15 g","serving_quantity":15,"nutriments":{"energy-kcal_100g":539,"proteins_100g":6.3,"fat_100g":30.9,)"
    R"("carbohydrates_100g":57.5,"sugars_100g":56.3,"salt_100g":0.1075}}})";

class FoodApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::FoodController controller;
    ScriptedTransport off;

    std::string config_file_name() const override { return "food_api_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override { cfg["food"]["enabled"] = true; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Food::Http::install_for_testing(&off);
        Database::get().execute_write([](auto& txn) {
            txn.exec(
                "TRUNCATE TABLE food_entries, food_items, food_goals, mi_accounts, body_measurements, "
                "daily_activity");
            for (const char* id : {kAnna, kBoris}) {
                txn.exec_params(
                    "INSERT INTO users (id, email, confirmed, role_id) "
                    "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                    "ON CONFLICT DO NOTHING",
                    std::string(id),
                    std::string(id) + "@example.test");
            }
            return true;
        });
        // The search cache would answer for a query of an earlier test.
        if (Cache::is_initialized()) {
            Cache::get().del("food:off:nutella");
        }
    }

    void TearDown() override {
        Food::Http::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    using Handler = void (Api::FoodController::*)(const HttpRequestPtr&, Api::FoodController::Callback&&);
    using IdHandler = void (Api::FoodController::*)(const HttpRequestPtr&,
                                                    Api::FoodController::Callback&&,
                                                    const std::string&);

    HttpResponsePtr call(Handler h,
                         const Security::Auth::AuthPrincipal& p,
                         HttpMethod m,
                         const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto req = TestHelpers::authed(p, m);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        (controller.*h)(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    HttpResponsePtr call_json(Handler h,
                              const Security::Auth::AuthPrincipal& p,
                              const json& body,
                              HttpMethod m = Post) {
        HttpResponsePtr captured;
        (controller.*h)(TestHelpers::authed_json(p, body, m), [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    HttpResponsePtr call_id(IdHandler h,
                            const Security::Auth::AuthPrincipal& p,
                            HttpMethod m,
                            const std::string& id,
                            const json& body = json::object()) {
        HttpResponsePtr captured;
        (controller.*h)(
            TestHelpers::authed_json(p, body, m), [&](const HttpResponsePtr& r) { captured = r; }, id);
        EXPECT_TRUE(captured);
        return captured;
    }

    json create_item(const Security::Auth::AuthPrincipal& p, const json& body) {
        const auto resp = call_json(&Api::FoodController::createItem, p, body);
        EXPECT_EQ(resp->statusCode(), k201Created) << resp->body();
        return body_of(resp)["data"];
    }

    static json egg() {
        return json{{"name", "Egg"},
                    {"kcal", 155},
                    {"protein_g", 13},
                    {"fat_g", 11},
                    {"carbs_g", 1.1},
                    {"servings", json::array({json{{"label", "1 egg"}, {"grams", 60}}})}};
    }

    static json owner_profile() {
        return json{{"height_cm", 170},
                    {"birth_date", "1991-03-15"},
                    {"sex", "male"},
                    {"activity", "light"},
                    {"target_weight_kg", 75},
                    {"pace_kg_per_week", 0.75},
                    {"manual_weight_kg", 91.9}};
    }
};

}  // namespace

// ── items ────────────────────────────────────────────────────────────────────

TEST_F(FoodApiTest, ItemsBelongToTheirOwner) {
    const json egg_a = create_item(user(kAnna), egg());
    EXPECT_EQ(egg_a["source"], "custom");
    EXPECT_EQ(egg_a["servings"][0]["grams"], 60);
    EXPECT_TRUE(egg_a["fiber_g"].is_null());

    const auto list_a = body_of(call(&Api::FoodController::listItems, user(kAnna), Get, {{"q", "eg"}}));
    EXPECT_EQ(list_a["total"], 1);
    EXPECT_EQ(body_of(call(&Api::FoodController::listItems, user(kBoris), Get))["total"], 0);

    const std::string id = egg_a["id"].get<std::string>();
    EXPECT_EQ(call_id(&Api::FoodController::updateItem, user(kBoris), Patch, id, json{{"name", "Mine"}})->statusCode(),
              k404NotFound);
    EXPECT_EQ(call_id(&Api::FoodController::deleteItem, user(kBoris), Delete, id)->statusCode(), k404NotFound);
    // Boris cannot log Anna's product either.
    const auto foreign = call_json(&Api::FoodController::createEntry,
                                   user(kBoris),
                                   json{{"date", "2026-10-06"}, {"meal", "lunch"}, {"item_id", id}, {"grams", 60}});
    EXPECT_EQ(foreign->statusCode(), k404NotFound);
    EXPECT_EQ(body_of(call(
                  &Api::FoodController::day, user(kBoris), Get, {{"date", "2026-10-06"}}))["data"]["totals"]["entries"],
              0);

    const auto patched = call_id(&Api::FoodController::updateItem,
                                 user(kAnna),
                                 Patch,
                                 id,
                                 json{{"name", "Egg L"}, {"fiber_g", nullptr}, {"salt_g", 0.4}});
    ASSERT_EQ(patched->statusCode(), k200OK) << patched->body();
    EXPECT_EQ(body_of(patched)["data"]["name"], "Egg L");
    EXPECT_DOUBLE_EQ(body_of(patched)["data"]["salt_g"].get<double>(), 0.4);
}

TEST_F(FoodApiTest, ItemInputIsValidated) {
    EXPECT_EQ(call_json(&Api::FoodController::createItem, user(kAnna), json{{"name", "x"}})->statusCode(),
              k400BadRequest);
    EXPECT_EQ(call_json(&Api::FoodController::createItem, user(kAnna), json{{"name", "x"}, {"kcal", -1}})->statusCode(),
              k400BadRequest);
    EXPECT_EQ(call_json(&Api::FoodController::createItem, user(kAnna), json{{"name", "x"}, {"kcal", 1}, {"per", "cup"}})
                  ->statusCode(),
              k400BadRequest);
    json bad_servings = egg();
    bad_servings["servings"] = json::array({json{{"label", ""}, {"grams", 10}}});
    EXPECT_EQ(call_json(&Api::FoodController::createItem, user(kAnna), bad_servings)->statusCode(), k400BadRequest);
    EXPECT_EQ(
        call_id(&Api::FoodController::updateItem, user(kAnna), Patch, "not-a-uuid", json{{"name", "x"}})->statusCode(),
        k400BadRequest);
}

TEST_F(FoodApiTest, DeleteArchivesAUsedProduct) {
    const json item = create_item(user(kAnna), egg());
    const std::string id = item["id"].get<std::string>();
    ASSERT_EQ(call_json(&Api::FoodController::createEntry,
                        user(kAnna),
                        json{{"date", "2026-10-06"}, {"meal", "breakfast"}, {"item_id", id}, {"grams", 120}})
                  ->statusCode(),
              k201Created);
    const auto removed = call_id(&Api::FoodController::deleteItem, user(kAnna), Delete, id);
    ASSERT_EQ(removed->statusCode(), k200OK);
    EXPECT_EQ(body_of(removed)["outcome"], "archived");
    EXPECT_EQ(body_of(call(&Api::FoodController::listItems, user(kAnna), Get))["total"], 0);
    EXPECT_EQ(body_of(call(&Api::FoodController::listItems, user(kAnna), Get, {{"archived", "true"}}))["total"], 1);
    const json day = body_of(call(&Api::FoodController::day, user(kAnna), Get, {{"date", "2026-10-06"}}))["data"];
    EXPECT_DOUBLE_EQ(day["meals"]["breakfast"][0]["kcal"].get<double>(), 186);

    const json unused = create_item(user(kAnna), egg());
    const auto gone = call_id(&Api::FoodController::deleteItem, user(kAnna), Delete, unused["id"].get<std::string>());
    EXPECT_EQ(body_of(gone)["outcome"], "deleted");
}

// ── diary ────────────────────────────────────────────────────────────────────

TEST_F(FoodApiTest, DayHoldsTheFourMealsWithTotalsAndTargets) {
    const json item = create_item(user(kAnna), egg());
    const std::string id = item["id"].get<std::string>();
    const auto from_item =
        call_json(&Api::FoodController::createEntry,
                  user(kAnna),
                  json{{"date", "2026-10-06"}, {"meal", "breakfast"}, {"item_id", id}, {"grams", 60}});
    ASSERT_EQ(from_item->statusCode(), k201Created) << from_item->body();
    EXPECT_EQ(body_of(from_item)["data"]["name"], "Egg");
    EXPECT_DOUBLE_EQ(body_of(from_item)["data"]["kcal"].get<double>(), 93);
    EXPECT_EQ(body_of(from_item)["data"]["estimated"], false);

    const auto quick = call_json(
        &Api::FoodController::createEntry,
        user(kAnna),
        json{{"date", "2026-10-06"}, {"meal", "lunch"}, {"name", "Street food"}, {"kcal", 500}, {"protein_g", 20}});
    ASSERT_EQ(quick->statusCode(), k201Created) << quick->body();
    EXPECT_TRUE(body_of(quick)["data"]["item_id"].is_null());
    EXPECT_EQ(body_of(quick)["data"]["estimated"], true);
    for (const char* meal : {"dinner", "snack"}) {
        ASSERT_EQ(call_json(&Api::FoodController::createEntry,
                            user(kAnna),
                            json{{"date", "2026-10-06"}, {"meal", meal}, {"name", "x"}, {"kcal", 100}})
                      ->statusCode(),
                  k201Created);
    }

    const auto resp = call(&Api::FoodController::day, user(kAnna), Get, {{"date", "2026-10-06"}});
    ASSERT_EQ(resp->statusCode(), k200OK) << resp->body();
    const json day = body_of(resp)["data"];
    EXPECT_EQ(day["date"], "2026-10-06");
    EXPECT_EQ(day["meals"]["breakfast"].size(), 1u);
    EXPECT_EQ(day["meals"]["lunch"].size(), 1u);
    EXPECT_EQ(day["meals"]["dinner"].size(), 1u);
    EXPECT_EQ(day["meals"]["snack"].size(), 1u);
    EXPECT_DOUBLE_EQ(day["totals"]["kcal"].get<double>(), 793);
    EXPECT_DOUBLE_EQ(day["totals"]["protein_g"].get<double>(), 27.8);
    EXPECT_EQ(day["totals"]["entries"], 4);
    EXPECT_TRUE(day["active_kcal"].is_null());
    EXPECT_TRUE(day["targets"].is_null()) << "no profile yet";

    // Edit grams: the numbers follow the item; move to another day.
    const std::string entry_id = body_of(from_item)["data"]["id"].get<std::string>();
    const auto edited = call_id(&Api::FoodController::updateEntry,
                                user(kAnna),
                                Patch,
                                entry_id,
                                json{{"grams", 120}, {"date", "2026-10-07"}, {"note", "two eggs"}});
    ASSERT_EQ(edited->statusCode(), k200OK) << edited->body();
    EXPECT_DOUBLE_EQ(body_of(edited)["data"]["kcal"].get<double>(), 186);
    EXPECT_EQ(body_of(edited)["data"]["note"], "two eggs");
    EXPECT_EQ(body_of(call(
                  &Api::FoodController::day, user(kAnna), Get, {{"date", "2026-10-06"}}))["data"]["totals"]["entries"],
              3);

    const json week = body_of(call(&Api::FoodController::week, user(kAnna), Get, {{"from", "2026-10-05"}}))["data"];
    ASSERT_EQ(week["days"].size(), 7u);
    EXPECT_EQ(week["days"][1]["entries"], 3);
    EXPECT_DOUBLE_EQ(week["days"][2]["kcal"].get<double>(), 186);

    EXPECT_EQ(
        call_id(&Api::FoodController::updateEntry, user(kBoris), Patch, entry_id, json{{"grams", 1}})->statusCode(),
        k404NotFound);
    EXPECT_EQ(call_id(&Api::FoodController::deleteEntry, user(kBoris), Delete, entry_id)->statusCode(), k404NotFound);
    EXPECT_EQ(call_id(&Api::FoodController::deleteEntry, user(kAnna), Delete, entry_id)->statusCode(), k200OK);
    EXPECT_EQ(call_id(&Api::FoodController::deleteEntry, user(kAnna), Delete, entry_id)->statusCode(), k404NotFound);
}

TEST_F(FoodApiTest, EntryInputIsValidated) {
    const auto post = [&](const json& b) {
        return call_json(&Api::FoodController::createEntry, user(kAnna), b)->statusCode();
    };
    EXPECT_EQ(post(json{{"date", "2026-10-06"}, {"meal", "brunch"}, {"name", "x"}, {"kcal", 1}}), k400BadRequest);
    EXPECT_EQ(post(json{{"date", "06.10.2026"}, {"meal", "lunch"}, {"name", "x"}, {"kcal", 1}}), k400BadRequest);
    EXPECT_EQ(post(json{{"date", "2026-13-40"}, {"meal", "lunch"}, {"name", "x"}, {"kcal", 1}}), k400BadRequest);
    EXPECT_EQ(post(json{{"date", "2026-10-06"}, {"meal", "lunch"}, {"name", "x"}}), k400BadRequest) << "kcal required";
    EXPECT_EQ(post(json{{"date", "2026-10-06"}, {"meal", "lunch"}, {"kcal", 5}}), k400BadRequest) << "name required";
    EXPECT_EQ(post(json{{"date", "2026-10-06"}, {"meal", "lunch"}, {"item_id", kMissingId}}), k400BadRequest)
        << "grams required with an item";
    EXPECT_EQ(post(json{{"date", "2026-10-06"}, {"meal", "lunch"}, {"item_id", kMissingId}, {"grams", 10}}),
              k404NotFound);
    EXPECT_EQ(post(json{{"date", "2026-10-06"}, {"meal", "lunch"}, {"name", "x"}, {"kcal", -5}}), k400BadRequest);
    EXPECT_EQ(call(&Api::FoodController::day, user(kAnna), Get, {{"date", "junk"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(call(&Api::FoodController::day, user(kAnna), Get, {{"date", "2026-02-30"}})->statusCode(),
              k400BadRequest);
    EXPECT_EQ(call(&Api::FoodController::week, user(kAnna), Get)->statusCode(), k400BadRequest);
}

TEST_F(FoodApiTest, BatchWritesAllOrNothing) {
    const json item = create_item(user(kAnna), egg());
    json entries = json::array({
        json{{"date", "2026-10-06"}, {"meal", "dinner"}, {"item_id", item["id"]}, {"grams", 60}},
        json{{"date", "2026-10-06"}, {"meal", "dinner"}, {"name", "Rice"}, {"kcal", 300}, {"carbs_g", 65}},
    });
    const auto ok = call_json(&Api::FoodController::createEntries, user(kAnna), json{{"entries", entries}});
    ASSERT_EQ(ok->statusCode(), k201Created) << ok->body();
    EXPECT_EQ(body_of(ok)["count"], 2);
    EXPECT_EQ(body_of(ok)["data"][1]["position"], 2);

    entries.push_back(json{{"date", "2026-10-06"}, {"meal", "dinner"}, {"name", "Bad"}});
    const auto bad = call_json(&Api::FoodController::createEntries, user(kAnna), json{{"entries", entries}});
    EXPECT_EQ(bad->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(bad)["errors"][0]["field"], "entries[2].kcal");
    EXPECT_EQ(body_of(call(
                  &Api::FoodController::day, user(kAnna), Get, {{"date", "2026-10-06"}}))["data"]["totals"]["entries"],
              2);

    EXPECT_EQ(
        call_json(&Api::FoodController::createEntries, user(kAnna), json{{"entries", json::array()}})->statusCode(),
        k400BadRequest);
}

// ── goals ────────────────────────────────────────────────────────────────────

TEST_F(FoodApiTest, GoalsAreComputedFromTheProfileAndOverridden) {
    const auto empty = call(&Api::FoodController::getGoals, user(kAnna), Get);
    ASSERT_EQ(empty->statusCode(), k200OK) << empty->body();
    EXPECT_TRUE(body_of(empty)["data"]["computed"].is_null());
    EXPECT_EQ(body_of(empty)["data"]["weight"]["source"], "none");
    EXPECT_EQ(body_of(empty)["data"]["missing"].size(), 5u);

    const auto saved = call_json(&Api::FoodController::putGoals, user(kAnna), owner_profile(), Put);
    ASSERT_EQ(saved->statusCode(), k200OK) << saved->body();
    const json g = body_of(saved)["data"];
    EXPECT_EQ(g["weight"]["source"], "manual");
    EXPECT_TRUE(g["missing"].empty());
    EXPECT_EQ(g["computed"]["age_years"], 35);
    EXPECT_EQ(g["computed"]["bmr"], 1812);
    EXPECT_EQ(g["computed"]["kcal"], 1667);
    EXPECT_EQ(g["computed"]["below_bmr"], true);
    EXPECT_EQ(g["targets"]["kcal"], 1667);
    EXPECT_EQ(g["targets"]["protein_g"], 135);

    json with_overrides = owner_profile();
    with_overrides["kcal_override"] = 1670;
    with_overrides["protein_override_g"] = 150;
    const json t = body_of(call_json(&Api::FoodController::putGoals, user(kAnna), with_overrides, Put))["data"];
    EXPECT_EQ(t["targets"]["kcal"], 1670);
    EXPECT_EQ(t["targets"]["protein_g"], 150);
    EXPECT_EQ(t["targets"]["fat_g"], 46);
    EXPECT_EQ(t["computed"]["kcal"], 1667) << "the computed block stays as the reference";

    // The day carries the targets; Boris has none.
    EXPECT_EQ(
        body_of(call(&Api::FoodController::day, user(kAnna), Get, {{"date", "2026-10-06"}}))["data"]["targets"]["kcal"],
        1670);
    EXPECT_TRUE(body_of(call(&Api::FoodController::getGoals, user(kBoris), Get))["data"]["computed"].is_null());
}

TEST_F(FoodApiTest, GoalsInputIsValidated) {
    const auto put = [&](json p) {
        return call_json(&Api::FoodController::putGoals, user(kAnna), p, Put)->statusCode();
    };
    json p = owner_profile();
    p["birth_date"] = "2099-01-01";
    EXPECT_EQ(put(p), k400BadRequest);
    p = owner_profile();
    p["height_cm"] = 0;
    EXPECT_EQ(put(p), k400BadRequest);
    p = owner_profile();
    p["pace_kg_per_week"] = 5;
    EXPECT_EQ(put(p), k400BadRequest);
    p = owner_profile();
    p["sex"] = "yes";
    EXPECT_EQ(put(p), k400BadRequest);
    p = owner_profile();
    p["kcal_override"] = 100;
    EXPECT_EQ(put(p), k400BadRequest);
    EXPECT_EQ(put(json::object()), k200OK) << "an empty profile is allowed; the goal is just not computed";
}

TEST_F(FoodApiTest, WeightAndActiveKcalComeFromTheMiScale) {
    Database::get().execute_write([](auto& txn) {
        txn.exec(std::string("INSERT INTO mi_accounts (owner_id, xiaomi_user_id, pass_token_sealed, nonce) VALUES ('") +
                 kAnna + "', 'food-a', 'x', 'x')");
        txn.exec("INSERT INTO body_measurements (user_id, timestamp, weight_kg) VALUES ('food-a', now(), 90)");
        txn.exec(
            "INSERT INTO daily_activity (user_id, date, steps, active_kcal) VALUES ('food-a', '2026-10-06', 5000, "
            "321)");
        return true;
    });
    const json g = body_of(call_json(&Api::FoodController::putGoals, user(kAnna), owner_profile(), Put))["data"];
    EXPECT_EQ(g["weight"]["source"], "scale");
    EXPECT_DOUBLE_EQ(g["weight"]["kg"].get<double>(), 90);
    EXPECT_EQ(g["computed"]["bmr"], 1793);  // 900 + 1062.5 - 175 + 5
    const json day = body_of(call(&Api::FoodController::day, user(kAnna), Get, {{"date", "2026-10-06"}}))["data"];
    EXPECT_DOUBLE_EQ(day["active_kcal"].get<double>(), 321);
}

// ── Open Food Facts ──────────────────────────────────────────────────────────

TEST_F(FoodApiTest, SearchProxiesAndCachesOpenFoodFacts) {
    off.replies.push_back({200, R"({"hits":[{"code":"1","product_name":"Nutella","brands":["Ferrero"],
        "nutriments":{"energy-kcal_100g":539,"proteins_100g":6.3,"fat_100g":30.9,"carbohydrates_100g":57.5}}]})"});
    const auto first = call(&Api::FoodController::offSearch, user(kAnna), Get, {{"q", "Nutella"}});
    ASSERT_EQ(first->statusCode(), k200OK) << first->body();
    EXPECT_EQ(body_of(first)["count"], 1);
    EXPECT_EQ(body_of(first)["data"][0]["brand"], "Ferrero");
    EXPECT_EQ(body_of(first)["cached"], false);
    // The same query again does not reach the service.
    const auto second = call(&Api::FoodController::offSearch, user(kBoris), Get, {{"q", "nutella"}});
    ASSERT_EQ(second->statusCode(), k200OK);
    EXPECT_EQ(body_of(second)["cached"], true);
    EXPECT_EQ(off.urls.size(), 1u);

    EXPECT_EQ(call(&Api::FoodController::offSearch, user(kAnna), Get)->statusCode(), k400BadRequest);
    off.fail = true;
    const auto down = call(&Api::FoodController::offSearch, user(kAnna), Get, {{"q", "something else"}});
    EXPECT_EQ(down->statusCode(), k503ServiceUnavailable);
    EXPECT_EQ(body_of(down)["error"], "upstream_unavailable");
}

TEST_F(FoodApiTest, FromOffCopiesAProductOnce) {
    off.replies.push_back({200, kProductAnswer});
    const auto created = call_json(&Api::FoodController::itemFromOff, user(kAnna), json{{"code", "3017624010701"}});
    ASSERT_EQ(created->statusCode(), k201Created) << created->body();
    const json item = body_of(created)["data"];
    EXPECT_EQ(item["source"], "off");
    EXPECT_EQ(item["name"], "Nutella");
    EXPECT_EQ(item["off_code"], "3017624010701");
    EXPECT_EQ(item["servings"][0]["label"], "15 g");
    EXPECT_EQ(item["servings"][0]["grams"], 15);

    off.replies.push_back({200, kProductAnswer});
    const auto again = call_json(&Api::FoodController::itemFromOff, user(kAnna), json{{"code", "3017624010701"}});
    EXPECT_EQ(again->statusCode(), k200OK);
    EXPECT_EQ(body_of(again)["data"]["id"], item["id"]);
    EXPECT_EQ(body_of(call(&Api::FoodController::listItems, user(kAnna), Get))["total"], 1);

    off.replies.push_back({200, R"({"status":0,"status_verbose":"product not found"})"});
    EXPECT_EQ(call_json(&Api::FoodController::itemFromOff, user(kAnna), json{{"code", "1"}})->statusCode(),
              k404NotFound);
    EXPECT_EQ(call_json(&Api::FoodController::itemFromOff, user(kAnna), json{{"code", "abc"}})->statusCode(),
              k400BadRequest);
}

// ── guards ───────────────────────────────────────────────────────────────────

TEST_F(FoodApiTest, RoutesNeedAUserAccount) {
    const auto bearer = user("static-bearer");
    const auto resp = call(&Api::FoodController::getGoals, bearer, Get);
    EXPECT_EQ(resp->statusCode(), k403Forbidden);
    EXPECT_EQ(body_of(resp)["error"], "no_user_account");

    HttpResponsePtr anonymous;
    controller.listItems(TestHelpers::make_request(Get), [&](const HttpResponsePtr& r) { anonymous = r; });
    ASSERT_TRUE(anonymous);
    EXPECT_NE(anonymous->statusCode(), k200OK);
}

namespace {

class FoodDisabledTest : public TestHelpers::CoreBackedTest {
protected:
    Api::FoodController controller;
    std::string config_file_name() const override { return "food_disabled_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["food"]["enabled"] = false; }
};

}  // namespace

TEST_F(FoodDisabledTest, EveryRouteIs404WhileTheModuleIsOff) {
    HttpResponsePtr resp;
    controller.getGoals(TestHelpers::authed(user(kAnna), Get), [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_TRUE(resp);
    EXPECT_EQ(resp->statusCode(), k404NotFound);
    resp.reset();
    controller.day(TestHelpers::authed(user(kAnna), Get), [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_TRUE(resp);
    EXPECT_EQ(resp->statusCode(), k404NotFound);
}
