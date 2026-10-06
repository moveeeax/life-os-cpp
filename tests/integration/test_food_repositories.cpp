/**
 * @file test_food_repositories.cpp
 * @brief Food items, the diary and the goals profile: ownership, copied
 *        numbers, archive-on-delete, day and week views, the Mi scale weight.
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/RepoErrors.hpp"
#include "repositories/food/EntryRepository.hpp"
#include "repositories/food/GoalsRepository.hpp"
#include "repositories/food/ItemRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa4";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb4";

class FoodRepositoriesTest : public TestHelpers::CoreBackedTest {
protected:
    Repositories::ItemRepository items;
    Repositories::EntryRepository entries;
    Repositories::GoalsRepository goals;

    std::string config_file_name() const override { return "food_repositories_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec(
                "TRUNCATE TABLE food_entries, food_items, food_goals, food_parse_jobs, mi_accounts, "
                "body_measurements, daily_activity");
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
    }

    static void sql(const std::string& statement) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec(statement);
            return true;
        });
    }

    static Repositories::ItemRepository::Input nutella() {
        Repositories::ItemRepository::Input in;
        in.name = "Nutella";
        in.brand = "Ferrero";
        in.kcal = 539;
        in.protein_g = 6.3;
        in.fat_g = 30.9;
        in.carbs_g = 57.5;
        in.sugar_g = 56.3;
        in.salt_g = 0.1075;
        in.off_code = "3017624010701";
        in.servings_json = R"([{"label":"1 tbsp","grams":15}])";
        return in;
    }

    static Repositories::ItemRepository::Input egg() {
        Repositories::ItemRepository::Input in;
        in.name = "Egg";
        in.kcal = 155;
        in.protein_g = 13;
        in.fat_g = 11;
        in.carbs_g = 1.1;
        return in;
    }

    /// An entry of @p grams of the item, numbers from the repository.
    Repositories::EntryRepository::Input entry_of(const std::string& owner,
                                                  const json& item,
                                                  double grams,
                                                  const std::string& meal,
                                                  const std::string& date = "2026-10-06") {
        const auto n = entries.from_item(owner, item["id"].get<std::string>(), grams);
        EXPECT_TRUE(n.has_value());
        Repositories::EntryRepository::Input in;
        in.date = date;
        in.meal = meal;
        in.name = n->name;
        in.item_id = item["id"].get<std::string>();
        in.grams = grams;
        in.kcal = n->kcal;
        in.protein_g = n->protein_g;
        in.fat_g = n->fat_g;
        in.carbs_g = n->carbs_g;
        in.fiber_g = n->fiber_g;
        in.sugar_g = n->sugar_g;
        in.salt_g = n->salt_g;
        return in;
    }

    static Repositories::EntryRepository::Input quick(const std::string& meal,
                                                      double kcal,
                                                      const std::string& date = "2026-10-06") {
        Repositories::EntryRepository::Input in;
        in.date = date;
        in.meal = meal;
        in.name = "Street food";
        in.kcal = kcal;
        in.estimated = true;
        return in;
    }
};

}  // namespace

TEST_F(FoodRepositoriesTest, ItemsAreScopedByOwnerAndSearchedByNameAndBrand) {
    items.create(kAnna, nutella());
    items.create(kAnna, egg());
    items.create(kBoris, egg());

    EXPECT_EQ(items.list(kAnna, "", false, 50, 0).total, 2);
    EXPECT_EQ(items.list(kAnna, "FERR", false, 50, 0).total, 1);
    EXPECT_EQ(items.list(kAnna, "nut", false, 50, 0).rows[0]["name"], "Nutella");
    EXPECT_EQ(items.list(kBoris, "", false, 50, 0).total, 1);
    const auto anna_item = items.list(kAnna, "nut", false, 50, 0).rows[0]["id"].get<std::string>();
    EXPECT_FALSE(items.find(kBoris, anna_item).has_value());
    EXPECT_THROW(items.remove(kBoris, anna_item), Repositories::FoodItemNotFound);
    EXPECT_THROW(items.update(kBoris, anna_item, {}), Repositories::FoodItemNotFound);
}

TEST_F(FoodRepositoriesTest, SearchTreatsPercentAndUnderscoreAsText) {
    auto fifty = egg();
    fifty.name = "Yogurt 50% less sugar";
    items.create(kAnna, fifty);
    items.create(kAnna, nutella());
    EXPECT_EQ(items.list(kAnna, "50%", false, 50, 0).total, 1);
    EXPECT_EQ(items.list(kAnna, "%", false, 50, 0).total, 1) << "a bare % matches the % product, not everything";
    EXPECT_EQ(items.list(kAnna, "_", false, 50, 0).total, 0);
}

TEST_F(FoodRepositoriesTest, PromptItemsComeNewestFirstWithoutArchived) {
    const auto old = items.create(kAnna, egg());
    items.create(kAnna, nutella());
    Repositories::ItemRepository::Patch touch;
    touch.brand = "Fresh";
    items.update(kAnna, old["id"], touch);
    auto archived = egg();
    archived.name = "Gone";
    const auto gone = items.create(kAnna, archived);
    Repositories::ItemRepository::Patch hide;
    hide.archived = true;
    items.update(kAnna, gone["id"], hide);

    const auto rows = items.for_prompt(kAnna, 10);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0]["id"], old["id"]) << "the item touched last comes first";
    EXPECT_EQ(items.for_prompt(kAnna, 1).size(), 1u);
}

TEST_F(FoodRepositoriesTest, UpsertOffReturnsTheExistingCopy) {
    const auto [first, created] = items.upsert_off(kAnna, nutella());
    EXPECT_TRUE(created);
    EXPECT_EQ(first["source"], "off");
    EXPECT_EQ(first["off_code"], "3017624010701");
    EXPECT_EQ(first["servings"][0]["grams"], 15);

    const auto [again, created_again] = items.upsert_off(kAnna, nutella());
    EXPECT_FALSE(created_again);
    EXPECT_EQ(again["id"], first["id"]);
    // Another user gets their own copy.
    EXPECT_TRUE(items.upsert_off(kBoris, nutella()).second);
    EXPECT_EQ(items.list(kAnna, "", false, 50, 0).total, 1);
}

TEST_F(FoodRepositoriesTest, EntryNumbersComeFromTheItemAndGrams) {
    const auto nut = items.create(kAnna, nutella());
    const auto n = entries.from_item(kAnna, nut["id"].get<std::string>(), 30);
    ASSERT_TRUE(n.has_value());
    EXPECT_DOUBLE_EQ(n->kcal, 161.7);
    EXPECT_DOUBLE_EQ(n->protein_g, 1.9);
    EXPECT_FALSE(n->fiber_g.has_value());
    EXPECT_DOUBLE_EQ(n->sugar_g.value(), 16.9);
    EXPECT_FALSE(entries.from_item(kBoris, nut["id"].get<std::string>(), 30).has_value());

    const auto e = entries.create(kAnna, entry_of(kAnna, nut, 100, "breakfast"));
    EXPECT_EQ(e["kcal"], 539);
    EXPECT_EQ(e["position"], 1);
    EXPECT_EQ(e["estimated"], false);
}

TEST_F(FoodRepositoriesTest, DayTotalsAndMealsAreOrdered) {
    const auto nut = items.create(kAnna, nutella());
    entries.create(kAnna, entry_of(kAnna, nut, 30, "breakfast"));
    entries.create(kAnna, quick("lunch", 500));
    entries.create(kAnna, entry_of(kAnna, nut, 30, "breakfast"));
    entries.create(kBoris, quick("dinner", 900));

    const json day = entries.day(kAnna, "2026-10-06");
    EXPECT_EQ(day["date"], "2026-10-06");
    ASSERT_EQ(day["meals"]["breakfast"].size(), 2u);
    EXPECT_EQ(day["meals"]["breakfast"][1]["position"], 2);
    EXPECT_EQ(day["meals"]["lunch"].size(), 1u);
    EXPECT_TRUE(day["meals"]["lunch"][0]["item_id"].is_null());
    EXPECT_EQ(day["meals"]["lunch"][0]["estimated"], true);
    EXPECT_TRUE(day["meals"]["dinner"].empty());
    EXPECT_TRUE(day["meals"]["snack"].empty());
    EXPECT_DOUBLE_EQ(day["totals"]["kcal"].get<double>(), 823.4);
    EXPECT_EQ(day["totals"]["entries"], 3);
    EXPECT_TRUE(day["totals"]["fiber_g"].is_null());
    EXPECT_EQ(entries.day(kBoris, "2026-10-06")["totals"]["entries"], 1);
    EXPECT_THROW(entries.day(kAnna, "junk"), Repositories::ValidationError);
}

TEST_F(FoodRepositoriesTest, WeekHasSevenRowsWithZerosForEmptyDays) {
    entries.create(kAnna, quick("lunch", 500));
    const json week = entries.week(kAnna, "2026-10-05");
    ASSERT_EQ(week.size(), 7u);
    EXPECT_EQ(week[0]["date"], "2026-10-05");
    EXPECT_EQ(week[0]["kcal"], 0);
    EXPECT_EQ(week[1]["kcal"], 500);
    EXPECT_EQ(week[1]["entries"], 1);
    EXPECT_EQ(week[6]["date"], "2026-10-11");
    EXPECT_EQ(entries.week(kBoris, "2026-10-05")[1]["kcal"], 0);
}

TEST_F(FoodRepositoriesTest, UpdateGramsRecomputesFromTheItemButNotForAQuickEntry) {
    const auto nut = items.create(kAnna, nutella());
    const auto e = entries.create(kAnna, entry_of(kAnna, nut, 30, "breakfast"));
    const auto q = entries.create(kAnna, quick("lunch", 500));

    Repositories::EntryRepository::Patch grams;
    grams.grams = 60;
    EXPECT_DOUBLE_EQ(entries.update(kAnna, e["id"], grams)["kcal"].get<double>(), 323.4);
    EXPECT_EQ(entries.update(kAnna, q["id"], grams)["kcal"], 500);

    Repositories::EntryRepository::Patch move;
    move.meal = "dinner";
    move.date = "2026-10-07";
    // The target meal already has two entries: the moved one lands after them.
    entries.create(kAnna, quick("dinner", 100, "2026-10-07"));
    entries.create(kAnna, quick("dinner", 200, "2026-10-07"));
    const auto moved = entries.update(kAnna, e["id"], move);
    EXPECT_EQ(moved["meal"], "dinner");
    EXPECT_EQ(moved["date"], "2026-10-07");
    const auto dinner = entries.day(kAnna, "2026-10-07")["meals"]["dinner"];
    ASSERT_EQ(dinner.size(), 3u);
    EXPECT_EQ(dinner[2]["id"], e["id"]) << "a moved entry goes to the end of its new meal";
    EXPECT_THROW(entries.update(kBoris, e["id"], grams), Repositories::FoodEntryNotFound);
    Repositories::EntryRepository::Patch bad;
    bad.date = "2026-13-40";
    EXPECT_THROW(entries.update(kAnna, q["id"], bad), Repositories::ValidationError);
    EXPECT_THROW(entries.remove(kBoris, e["id"]), Repositories::FoodEntryNotFound);
    entries.remove(kAnna, e["id"]);
    EXPECT_EQ(entries.day(kAnna, "2026-10-07")["totals"]["entries"], 0);
}

TEST_F(FoodRepositoriesTest, BatchWritesAllOrNothing) {
    std::vector<Repositories::EntryRepository::Input> many{quick("lunch", 100), quick("lunch", 200)};
    EXPECT_EQ(entries.create_many(kAnna, many).size(), 2u);
    many.push_back(quick("lunch", -1));  // violates the CHECK
    EXPECT_THROW(entries.create_many(kAnna, many), std::exception);
    EXPECT_EQ(entries.day(kAnna, "2026-10-06")["totals"]["entries"], 2);
}

TEST_F(FoodRepositoriesTest, RecentListsDistinctItemsByLastUse) {
    const auto nut = items.create(kAnna, nutella());
    const auto e = items.create(kAnna, egg());
    entries.create(kAnna, entry_of(kAnna, nut, 30, "breakfast"));
    entries.create(kAnna, entry_of(kAnna, e, 60, "breakfast"));
    entries.create(kAnna, entry_of(kAnna, nut, 30, "snack"));
    const json recent = items.recent(kAnna, 30);
    ASSERT_EQ(recent.size(), 2u);
    EXPECT_EQ(recent[0]["id"], nut["id"]);
    EXPECT_TRUE(items.recent(kBoris, 30).empty());
}

TEST_F(FoodRepositoriesTest, RemoveDeletesAnUnusedItemAndArchivesAUsedOne) {
    const auto nut = items.create(kAnna, nutella());
    const auto e = items.create(kAnna, egg());
    const auto entry = entries.create(kAnna, entry_of(kAnna, nut, 30, "breakfast"));

    EXPECT_EQ(items.remove(kAnna, e["id"]), "deleted");
    EXPECT_EQ(items.remove(kAnna, nut["id"]), "archived");
    EXPECT_EQ(items.list(kAnna, "", false, 50, 0).total, 0);
    EXPECT_EQ(items.list(kAnna, "", true, 50, 0).total, 1);
    EXPECT_TRUE(items.find(kAnna, nut["id"]).has_value());
    // The entry keeps its numbers and its item.
    const json day = entries.day(kAnna, "2026-10-06");
    EXPECT_EQ(day["meals"]["breakfast"][0]["id"], entry["id"]);
    EXPECT_DOUBLE_EQ(day["meals"]["breakfast"][0]["kcal"].get<double>(), 161.7);
    EXPECT_EQ(day["meals"]["breakfast"][0]["item_id"], nut["id"]);
}

TEST_F(FoodRepositoriesTest, PatchSetsAndClearsNullableNutrients) {
    const auto e = items.create(kBoris, egg());
    Repositories::ItemRepository::Patch p;
    p.name = "Egg L";
    p.fiber_g = std::optional<double>{1.5};
    const auto updated = items.update(kBoris, e["id"], p);
    EXPECT_EQ(updated["name"], "Egg L");
    EXPECT_DOUBLE_EQ(updated["fiber_g"].get<double>(), 1.5);
    EXPECT_EQ(updated["kcal"], 155);

    Repositories::ItemRepository::Patch clear;
    clear.fiber_g = std::optional<double>{};
    EXPECT_TRUE(items.update(kBoris, e["id"], clear)["fiber_g"].is_null());
}

TEST_F(FoodRepositoriesTest, GoalsProfileRoundTrips) {
    EXPECT_FALSE(goals.load(kAnna).has_value());
    Repositories::GoalsRepository::Profile p;
    p.height_cm = 170;
    p.birth_date = "1991-03-15";
    p.sex = "male";
    p.target_weight_kg = 75;
    p.pace_kg_per_week = 0.75;
    p.kcal_override = 1670;
    p.profile_note = "eats out";
    const json saved = goals.put(kAnna, p);
    EXPECT_EQ(saved["height_cm"], 170);
    EXPECT_EQ(saved["birth_date"], "1991-03-15");
    EXPECT_EQ(saved["kcal_override"], 1670);
    EXPECT_TRUE(saved["protein_override_g"].is_null());
    EXPECT_EQ(saved["profile_note"], "eats out");

    p.kcal_override.reset();
    EXPECT_TRUE(goals.put(kAnna, p)["kcal_override"].is_null());
    EXPECT_TRUE(goals.load(kAnna).has_value());
    EXPECT_FALSE(goals.load(kBoris).has_value());
}

TEST_F(FoodRepositoriesTest, ScaleWeightAndActiveKcalComeFromTheOwnersMiAccount) {
    sql(std::string("INSERT INTO mi_accounts (owner_id, xiaomi_user_id, pass_token_sealed, nonce) VALUES ('") + kAnna +
        "', 'fa', 'x', 'x')");
    sql("INSERT INTO body_measurements (user_id, timestamp, weight_kg) VALUES "
        "('fa', now() - interval '2 days', 90), ('fa', now(), 91.9), ('fb', now(), 70)");
    sql("INSERT INTO daily_activity (user_id, date, steps, active_kcal) VALUES "
        "('fa', '2026-10-06', 1000, 321), ('fb', '2026-10-06', 1, 999)");

    EXPECT_DOUBLE_EQ(goals.scale_weight(kAnna).value(), 91.9);
    EXPECT_FALSE(goals.scale_weight(kBoris).has_value());
    EXPECT_DOUBLE_EQ(goals.active_kcal(kAnna, "2026-10-06").value(), 321);
    EXPECT_FALSE(goals.active_kcal(kAnna, "2026-10-05").has_value());
    EXPECT_FALSE(goals.active_kcal(kBoris, "2026-10-06").has_value());
}
