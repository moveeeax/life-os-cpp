/**
 * @file test_travel_repositories.cpp
 * @brief The travel repositories against a real database: the owner scope of
 *        crossings, rules and trips, the two rule templates, linking only the
 *        owner's posted spends, a deleted trip leaving its tasks and spends
 *        unlinked, packing counted from the trip's tasks.
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/travel/CrossingRepository.hpp"
#include "repositories/travel/Errors.hpp"
#include "repositories/travel/RuleRepository.hpp"
#include "repositories/travel/SettingsRepository.hpp"
#include "repositories/travel/TripRepository.hpp"
#include "test_helpers.hpp"

namespace {

using json = nlohmann::json;
using namespace Repositories::Travel;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaac01";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbc02";

class TravelRepositoriesTest : public TestHelpers::CoreBackedTest {
protected:
    CrossingRepository crossings;
    RuleRepository rules;
    TripRepository trips;
    SettingsRepository settings;

    std::string config_file_name() const override { return "travel_repositories_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            // DELETE, not TRUNCATE: task_items and money_transactions reference travel_trips.
            for (const char* table :
                 {"travel_trip_items", "travel_trips", "travel_rules", "travel_crossings", "travel_settings"}) {
                txn.exec(std::string("DELETE FROM ") + table);
            }
            for (const char* id : {kAnna, kBoris}) {
                const std::string o = id;
                txn.exec_params("DELETE FROM task_items WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM money_transactions WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM money_accounts WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM money_categories WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM money_currencies WHERE owner_id = $1::uuid", o);
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

    /// One posted spend of @p owner on @p date; its id.
    static std::string spend(const char* owner, const char* date, const char* type = "expense") {
        return Database::get().execute_write([&](auto& txn) {
            const std::string o = owner;
            txn.exec_params(
                "INSERT INTO money_currencies (owner_id, code) VALUES ($1::uuid, 'THB') ON CONFLICT DO NOTHING", o);
            auto r = txn.exec_params(
                "WITH a AS (INSERT INTO money_accounts (owner_id, name, currency) VALUES ($1::uuid, 'Cash', 'THB') "
                " RETURNING id), c AS (INSERT INTO money_categories (owner_id, name, kind) VALUES ($1::uuid, 'Food', "
                " $3) RETURNING id) "
                "INSERT INTO money_transactions (owner_id, type, date, account_id, category_id, amount, name) "
                "SELECT $1::uuid, $3, $2::date, a.id, c.id, 350, 'Lunch' FROM a, c RETURNING id::text",
                o,
                std::string(date),
                std::string(type));
            return r[0][0].template as<std::string>();
        });
    }

    std::string trip(const char* owner) {
        return trips.create(owner, {"Thailand", "2026-11-01", "2026-11-20", {"TH"}, ""})["id"].get<std::string>();
    }
};

}  // namespace

TEST_F(TravelRepositoriesTest, CrossingsAreTheOwnersAlone) {
    const auto c = crossings.create(kAnna, {"2026-08-03", "KZ", "GE", std::string("23:40"), "ALA", ""});
    EXPECT_EQ(c["time"], "23:40");
    EXPECT_EQ(crossings.list(kBoris, 50, 0).size(), 0U);
    EXPECT_THROW(crossings.remove(kBoris, c["id"].get<std::string>()), NotFound);
    EXPECT_THROW(crossings.replace(kBoris, c["id"].get<std::string>(), {"2026-08-04", "KZ", "GE", {}, "", ""}),
                 NotFound);
    ASSERT_EQ(crossings.all(kAnna).size(), 1U);
    EXPECT_EQ(crossings.all(kAnna)[0].to, "GE");
}

TEST_F(TravelRepositoriesTest, ABadCountryCodeIsAnInvariant) {
    EXPECT_THROW(crossings.create(kAnna, {"2026-08-03", "kz", "GE", {}, "", ""}), Invariant);
    EXPECT_THROW(crossings.create(kAnna, {"2026-08-03", "KZ", "KZ", {}, "", ""}), Invariant) << "from = to";
}

TEST_F(TravelRepositoriesTest, TheTwoTemplates) {
    const auto schengen = RuleRepository::template_of("schengen");
    ASSERT_TRUE(schengen.has_value());
    const auto s = rules.create(kAnna, *schengen);
    EXPECT_EQ(s["window_kind"], "rolling_days");
    EXPECT_EQ(s["window_days"], 180);
    EXPECT_EQ(s["threshold_days"], 90);
    EXPECT_NE(s["source"].get<std::string>().find("2016/399"), std::string::npos);
    const auto tax = rules.create(kAnna, *RuleRepository::template_of("kz_tax"));
    EXPECT_EQ(tax["threshold_kind"], "over");
    EXPECT_TRUE(tax["window_days"].is_null());
    EXPECT_FALSE(RuleRepository::template_of("nope").has_value());
    const auto all = rules.all(kAnna);
    ASSERT_EQ(all.size(), 2U);
    EXPECT_TRUE(all[0].rule.countries.contains("AT")) << "SCHENGEN expands to its members";
    EXPECT_EQ(rules.list(kBoris).size(), 0U);
}

TEST_F(TravelRepositoriesTest, AHomeCountryIsKeptPerOwner) {
    EXPECT_FALSE(settings.home(kAnna).has_value());
    settings.put_home(kAnna, "KZ");
    settings.put_home(kAnna, "TH");
    EXPECT_EQ(settings.home(kAnna), "TH");
    EXPECT_FALSE(settings.home(kBoris).has_value());
}

TEST_F(TravelRepositoriesTest, OnlyTheOwnersPostedSpendsLink) {
    const auto t = trip(kAnna);
    const auto mine = spend(kAnna, "2026-11-03");
    const auto income = spend(kAnna, "2026-11-04", "income");
    const auto theirs = spend(kBoris, "2026-11-03");
    EXPECT_EQ(trips.candidate_spends(kAnna, t).size(), 1U) << "the expense of the dates, not the income";
    EXPECT_EQ(trips.link_spends(kAnna, t, {mine, income, theirs}, true), 1);
    ASSERT_EQ(trips.spends(kAnna, t).size(), 1U);
    EXPECT_THROW(trips.link_spends(kBoris, t, {theirs}, true), NotFound) << "not Boris's trip";
    EXPECT_EQ(trips.link_spends(kAnna, t, {mine}, false), 1);
    EXPECT_EQ(trips.spends(kAnna, t).size(), 0U);
}

TEST_F(TravelRepositoriesTest, ADeletedTripLeavesItsTasksAndSpends) {
    const auto t = trip(kAnna);
    const auto s = spend(kAnna, "2026-11-03");
    trips.link_spends(kAnna, t, {s}, true);
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params(
            "INSERT INTO task_items (owner_id, title, area, trip_id, status, completed_at) VALUES ($1::uuid, "
            "'Adapter', 'travel', $2::uuid, 'done', now()), ($1::uuid, 'Insurance', 'travel', $2::uuid, 'open', NULL)",
            std::string(kAnna),
            t);
        return true;
    });
    EXPECT_EQ(trips.packing(kAnna, t), (std::pair<int, int>{1, 2}));
    trips.add_item(kAnna, t, {"flight", "2026-11-01", std::string("23:40"), "ALA to BKK", "X7K2PD", "", ""});
    trips.remove(kAnna, t);
    const auto left = Database::get().execute_read([&](auto& txn) {
        auto r = txn.exec_params(
            "SELECT (SELECT count(*) FROM task_items WHERE owner_id = $1::uuid AND trip_id IS NULL), "
            " (SELECT count(*) FROM money_transactions WHERE owner_id = $1::uuid AND trip_id IS NULL), "
            " (SELECT count(*) FROM travel_trip_items WHERE owner_id = $1::uuid)",
            std::string(kAnna));
        return std::vector<long>{r[0][0].template as<long>(), r[0][1].template as<long>(), r[0][2].template as<long>()};
    });
    EXPECT_EQ(left, (std::vector<long>{2, 1, 0})) << "tasks and spends stay, unlinked; the plan goes";
}

TEST_F(TravelRepositoriesTest, TripsAndItemsAreTheOwnersAlone) {
    const auto t = trip(kAnna);
    EXPECT_FALSE(trips.get(kBoris, t).has_value());
    EXPECT_THROW(trips.add_item(kBoris, t, {"note", "2026-11-02", {}, "x", "", "", ""}), NotFound);
    TripRepository::Patch p;
    p.name = "Thailand and Laos";
    EXPECT_THROW(trips.update(kBoris, t, p), NotFound);
    EXPECT_EQ(trips.update(kAnna, t, p)["name"], "Thailand and Laos");
    p = {};
    p.end_date = "2026-10-01";
    EXPECT_THROW(trips.update(kAnna, t, p), Invariant) << "end before start";
    EXPECT_EQ(trips.next(kAnna, "2026-10-13").value()["id"], t);
    EXPECT_FALSE(trips.next(kAnna, "2026-11-02").has_value());
}
