/**
 * @file test_admin_flow.cpp
 * @brief Integration tests for AdminController.
 *
 * Exercises the require_admin guard (anonymous → 403, admin → ok) and
 * the self-protection rules (no self-delete, no self-role-change).
 */

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/AdminController.hpp"
#include "api/AuthController.hpp"
#include "domain/Role.hpp"
#include "repositories/RoleRepository.hpp"
#include "repositories/UserRepository.hpp"
#include "security/Auth.hpp"
#include "security/SessionStore.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kSecret = "test-jwt-secret-for-admin-flow-padding";

// Multi-bit masks used by the Roles-CRUD tests. Built from the named
// Domain::Permission bits so a backend bit-layout change shows up here.
constexpr std::uint32_t kEditorPerms = Domain::Permission::kGeneral | 0x02;                // 0x03
constexpr std::uint32_t kEditorPermsWidened = Domain::Permission::kGeneral | 0x02 | 0x04;  // 0x07

/// Snapshot of every role's permissions, written back on destruction.
struct RestoreRolePermissions {
    std::vector<std::pair<int, int>> saved;
    RestoreRolePermissions() {
        auto rows =
            Database::get().execute_read([](auto& txn) { return txn.exec("SELECT id, permissions FROM roles"); });
        for (const auto& row : rows)
            saved.emplace_back(row["id"].as<int>(), row["permissions"].as<int>());
    }
    ~RestoreRolePermissions() {
        try {
            Database::get().execute_write([&](auto& txn) {
                for (const auto& [role_id, perms] : saved)
                    txn.exec_params("UPDATE roles SET permissions = $1 WHERE id = $2", perms, role_id);
                return 0;
            });
        } catch (...) {}
    }
    RestoreRolePermissions(const RestoreRolePermissions&) = delete;
    RestoreRolePermissions& operator=(const RestoreRolePermissions&) = delete;
};

class AdminFlowTest : public TestHelpers::CoreBackedTest {
protected:
    Api::AuthController auth;
    Api::AdminController admin;

    std::string config_file_name() const override { return "admin_flow_test_config.json"; }

    void config_overrides(json& cfg) override {
        cfg["auth"]["mode"] = "jwt";
        cfg["auth"]["jwt"]["secret"] = kSecret;
        cfg["auth"]["cookies"]["enabled"] = true;
        cfg["auth"]["cookies"]["secure"] = false;
        cfg["mail"]["enabled"] = false;
        cfg["database"]["migrations_enabled"] = true;
        cfg["database"]["migrations_dir"] = "migrations";
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;

        // Extra roles from prior test bodies (e.g. "Editor") go with the wipe;
        // the two seed roles from migration 001 are kept/re-seeded.
        TestHelpers::wipe_app_data();
    }

    /// Create a user with the named role and return both the row and a
    /// pre-built principal that callers can stuff into req->attributes().
    struct Pair {
        Domain::User user;
        Security::Auth::AuthPrincipal principal;
    };

    Pair seed_user(const std::string& email, const std::string& role_name) {
        Repositories::RoleRepository roles;
        Repositories::UserRepository users;
        auto role = roles.find_by_name(role_name);
        if (!role) {
            // ASSERT_* is unusable in a value-returning helper, and EXPECT
            // followed by role->id would dereference an empty optional (UB).
            ADD_FAILURE() << "role " << role_name << " missing — seed migration?";
            throw std::runtime_error("seed role missing: " + role_name);
        }
        auto created = users.create(
            email, std::string("$argon2id$placeholder"), std::nullopt, std::nullopt, role->id, /*confirmed=*/true);
        auto fresh = users.find(created.id);
        Pair p;
        p.user = fresh ? *fresh : created;
        p.principal.subject = p.user.id;
        if (p.user.role)
            p.principal.roles.push_back(p.user.role->name);
        // Mirror what AuthController::mint_session would put in claims.
        p.principal.raw_claims = json{{"sub", p.user.id}, {"permissions", p.user.role ? p.user.role->permissions : 0u}};
        return p;
    }

    static HttpRequestPtr authed(const Security::Auth::AuthPrincipal& p) { return TestHelpers::authed(p); }

    static HttpRequestPtr authed_post(const Security::Auth::AuthPrincipal& p, const json& body) {
        return TestHelpers::authed_json(p, body);
    }
};

TEST_F(AdminFlowTest, listUsersForbiddenForRegularUser) {
    auto regular = seed_user("user@example.com", "User");
    HttpResponsePtr resp;
    admin.listUsers(authed(regular.principal), [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k403Forbidden);
}

TEST_F(AdminFlowTest, listUsersWorksForAdmin) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    seed_user("u1@example.com", "User");
    seed_user("u2@example.com", "User");
    HttpResponsePtr resp;
    admin.listUsers(authed(admin_user.principal), [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k200OK);
    auto body = json::parse(std::string(resp->body()));
    EXPECT_GE(body["total"].get<long>(), 3);
    EXPECT_TRUE(body["data"].is_array());
}

TEST_F(AdminFlowTest, createUserAsAdmin) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    auto req = authed_post(admin_user.principal, {{"email", "fresh@example.com"}, {"password", "password1"}});
    HttpResponsePtr resp;
    admin.createUser(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_EQ(resp->statusCode(), k201Created);
    Repositories::UserRepository repo;
    auto user = repo.find_by_email("fresh@example.com");
    ASSERT_TRUE(user.has_value());
    EXPECT_TRUE(user->confirmed);  // admin-created users are pre-confirmed
}

TEST_F(AdminFlowTest, createUserWritesAuditLog) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    auto req = authed_post(admin_user.principal, {{"email", "audited@example.com"}, {"password", "password1"}});
    HttpResponsePtr resp;
    admin.createUser(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_EQ(resp->statusCode(), k201Created);
    Repositories::UserRepository repo;
    auto created = repo.find_by_email("audited@example.com");
    ASSERT_TRUE(created.has_value());

    // Audit::record swallows exceptions, so a broken INSERT would otherwise pass
    // silently — assert the trail row actually landed, stamped with the actor.
    auto rows = Database::get().execute_read([&](auto& txn) {
        return txn.exec_params(
            "SELECT actor_id, action FROM audit_log "
            "WHERE action = 'user.create' AND target_id = $1",
            created->id);
    });
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0]["actor_id"].template as<std::string>(), admin_user.user.id);
}

TEST_F(AdminFlowTest, deleteUserAsAdmin) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    auto target = seed_user("victim@example.com", "User");
    HttpResponsePtr resp;
    admin.deleteUser(
        authed(admin_user.principal), [&](const HttpResponsePtr& r) { resp = r; }, target.user.id);
    ASSERT_EQ(resp->statusCode(), k200OK);
    Repositories::UserRepository repo;
    EXPECT_FALSE(repo.find(target.user.id).has_value());
}

namespace {

/// One row (or a few) in every module table that keeps owner_id, for @p owner.
void seed_module_rows(const std::string& owner) {
    Database::get().execute_write([&](auto& txn) {
        const auto run = [&](const char* sql) { txn.exec_params(sql, owner); };
        run("INSERT INTO money_currencies (owner_id, code) VALUES ($1::uuid, 'KZT')");
        run("WITH a AS (INSERT INTO money_accounts (owner_id, name, currency) VALUES ($1::uuid, 'Cash', 'KZT'), "
            " ($1::uuid, 'Card', 'KZT') RETURNING id), "
            "c AS (INSERT INTO money_categories (owner_id, name) VALUES ($1::uuid, 'Food') RETURNING id), "
            "t AS (INSERT INTO money_transactions (owner_id, type, date, account_id, amount, name, category_id) "
            " SELECT $1::uuid, 'expense', '2026-10-01', (SELECT min(id::text)::uuid FROM a), 10, 'Lunch', "
            " (SELECT id FROM c) RETURNING id) "
            "INSERT INTO money_transfers (owner_id, date, from_account_id, to_account_id, amount_sent) "
            "SELECT $1::uuid, '2026-10-01', (SELECT min(id::text)::uuid FROM a), (SELECT max(id::text)::uuid FROM a), "
            "5");
        run("INSERT INTO money_merchants (owner_id, merchant_key, display_name, last_seen) "
            "VALUES ($1::uuid, 'shop', 'Shop', '2026-10-01')");
        run("INSERT INTO money_settings (owner_id) VALUES ($1::uuid)");
        run("INSERT INTO money_parse_jobs (owner_id, kind, text, hint_date) VALUES ($1::uuid, 'text', 'x', "
            "'2026-10-01')");
        run("INSERT INTO money_advisor_reports (owner_id, period, period_start, period_end) "
            "VALUES ($1::uuid, 'week', '2026-10-05', '2026-10-11')");
        run("WITH g AS (INSERT INTO goal_items (owner_id, title, area, kind, start_date, due) "
            " VALUES ($1::uuid, 'Read', 'growth', 'binary', '2026-01-01', '2026-12-31') RETURNING id), "
            "ck AS (INSERT INTO goal_checkins (owner_id, goal_id, date, value) SELECT $1::uuid, id, '2026-10-01', 1 "
            "FROM g), "
            "sc AS (INSERT INTO goal_sections (owner_id, goal_id, name) SELECT $1::uuid, id, 'Part' FROM g RETURNING "
            "id), "
            "ms AS (INSERT INTO goal_milestones (owner_id, goal_id, date, label) "
            " SELECT $1::uuid, id, '2026-06-01', 'Half' FROM g) "
            "INSERT INTO task_items (owner_id, title, area, goal_id, goal_section_id) "
            "SELECT $1::uuid, 'Buy water', 'projects', (SELECT id FROM g), (SELECT id FROM sc)");
        run("WITH n AS (INSERT INTO task_notes (owner_id, text) VALUES ($1::uuid, 'note') RETURNING id) "
            "INSERT INTO task_parse_jobs (owner_id, text, hint_date, note_id) SELECT $1::uuid, 'x', '2026-10-01', id "
            "FROM n");
        run("WITH i AS (INSERT INTO food_items (owner_id, name, kcal) VALUES ($1::uuid, 'Rice', 130) RETURNING id), "
            "e AS (INSERT INTO food_entries (owner_id, date, meal, name, kcal, item_id) "
            " SELECT $1::uuid, '2026-10-01', 'lunch', 'Rice', 130, id FROM i), "
            "gl AS (INSERT INTO food_goals (owner_id) VALUES ($1::uuid)) "
            "INSERT INTO food_parse_jobs (owner_id, text, meal, date) VALUES ($1::uuid, 'rice', 'lunch', "
            "'2026-10-01')");
        run("INSERT INTO day_focus (owner_id, date, kind, text) VALUES ($1::uuid, '2026-10-01', 'text', 'Ship')");
        run("INSERT INTO travel_settings (owner_id, home_country) VALUES ($1::uuid, 'KZ')");
        run("INSERT INTO travel_crossings (owner_id, date, from_country, to_country) "
            "VALUES ($1::uuid, '2026-08-03', 'KZ', 'GE')");
        run("INSERT INTO travel_rules (owner_id, name, countries, window_kind, window_days, threshold_days, "
            "threshold_kind) VALUES ($1::uuid, 'Schengen', '{SCHENGEN}', 'rolling_days', 180, 90, 'at_most')");
        run("WITH t AS (INSERT INTO travel_trips (owner_id, name, start_date, end_date) "
            " VALUES ($1::uuid, 'Georgia', '2026-08-03', '2026-08-12') RETURNING id) "
            "INSERT INTO travel_trip_items (owner_id, trip_id, kind, date, title) "
            "SELECT $1::uuid, id, 'flight', '2026-08-03', 'ALA to TBS' FROM t");
        run("INSERT INTO travel_documents (owner_id, holder, kind, valid_until) "
            "VALUES ($1::uuid, 'Mikhail', 'passport', '2030-01-21')");
        run("INSERT INTO now_settings (owner_id, city, latitude, longitude) VALUES ($1::uuid, 'Almaty', 43.25, 76.91)");
        run("INSERT INTO day_note (owner_id, date, text) VALUES ($1::uuid, '2026-10-01', 'Call the bank')");
        run("WITH h AS (INSERT INTO habits (owner_id, name, schedule) VALUES ($1::uuid, 'Meditation', 'daily') "
            " RETURNING id) "
            "INSERT INTO habit_checks (owner_id, habit_id, date) SELECT $1::uuid, id, '2026-10-01' FROM h");
        run("INSERT INTO projects (owner_id, name, code, status) VALUES ($1::uuid, 'Life OS', 'life-os', 'active')");
        run("INSERT INTO notes (owner_id, slug, title) VALUES ($1::uuid, 'first-note', 'First note')");
        run("WITH c AS (INSERT INTO practice_clients (owner_id, name) VALUES ($1::uuid, 'Acme') RETURNING id), "
            "d AS (INSERT INTO practice_deals (owner_id, client_id, title) SELECT $1::uuid, id, 'Site' FROM c "
            " RETURNING id, client_id) "
            "INSERT INTO practice_entries (owner_id, kind, date, amount, currency, client_id, deal_id) "
            "SELECT $1::uuid, 'income', '2026-10-01', 100, 'USD', client_id, id FROM d");
        run("WITH x AS (INSERT INTO exercises (id, name, source, owner_id) "
            " VALUES ('custom-' || $1::text, 'Mine', 'custom', $1::uuid) RETURNING id), "
            "r AS (INSERT INTO routines (owner_id, name) VALUES ($1::uuid, 'Push') RETURNING id), "
            "re AS (INSERT INTO routine_exercises (routine_id, exercise_id, position) SELECT r.id, x.id, 1 FROM r, x), "
            "s AS (INSERT INTO workout_sessions (owner_id) VALUES ($1::uuid) RETURNING id) "
            "INSERT INTO session_exercises (session_id, exercise_id, position) SELECT s.id, x.id, 1 FROM s, x");
        return 0;
    });
}

/// Rows of @p owner in every table that has an owner_id column, and how many tables hold any.
std::pair<long, long> owned_rows(const std::string& owner) {
    return Database::get().execute_read([&](auto& txn) {
        auto tables = txn.exec(
            "SELECT table_name FROM information_schema.columns WHERE table_schema = 'public' "
            "AND column_name = 'owner_id' ORDER BY table_name");
        long rows = 0;
        long holding = 0;
        for (const auto& t : tables) {
            // The name comes from the catalog, not from input.
            auto n = txn.exec_params(
                "SELECT count(*) FROM \"" + t[0].template as<std::string>() + "\" WHERE owner_id = $1::uuid", owner);
            const long c = n[0][0].template as<long>();
            rows += c;
            holding += c > 0 ? 1 : 0;
        }
        return std::pair<long, long>{rows, holding};
    });
}

}  // namespace

TEST_F(AdminFlowTest, deleteUserRemovesEveryModuleRowTheyOwn) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    auto target = seed_user("leaver@example.com", "User");
    seed_module_rows(target.user.id);
    seed_module_rows(admin_user.user.id);
    const auto before = owned_rows(target.user.id);
    ASSERT_GE(before.second, 39) << "every module table got a row";

    HttpResponsePtr resp;
    admin.deleteUser(
        authed(admin_user.principal), [&](const HttpResponsePtr& r) { resp = r; }, target.user.id);
    ASSERT_EQ(resp->statusCode(), k200OK) << std::string(resp->body());

    EXPECT_EQ(owned_rows(target.user.id).first, 0) << "no orphan rows stay behind (no FK to users by design)";
    EXPECT_EQ(owned_rows(admin_user.user.id), before) << "another user's rows stay";

    // These tables have no FK to users, so the suite's TRUNCATE users would leave them behind.
    Repositories::UserRepository().remove(admin_user.user.id);
    EXPECT_EQ(owned_rows(admin_user.user.id).first, 0);
}

TEST_F(AdminFlowTest, deleteAndRoleChangeRevokeRefreshSessions) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    auto demoted = seed_user("demoted@example.com", "Administrator");
    auto deleted = seed_user("deleted@example.com", "User");
    const auto& cookies = Security::Auth::get().config().cookies;
    ASSERT_TRUE(Security::Sessions::record(cookies, demoted.user.id, "jti-demoted", 600));
    ASSERT_TRUE(Security::Sessions::record(cookies, deleted.user.id, "jti-deleted", 600));

    auto user_role = Repositories::RoleRepository().find_by_name("User");
    ASSERT_TRUE(user_role.has_value());
    auto patch = authed_post(admin_user.principal, {{"role_id", user_role->id}});
    patch->setMethod(Patch);
    HttpResponsePtr r1;
    admin.updateUser(
        patch, [&](const HttpResponsePtr& r) { r1 = r; }, demoted.user.id);
    ASSERT_EQ(r1->statusCode(), k200OK) << r1->body();
    EXPECT_FALSE(Security::Sessions::consume(cookies, "jti-demoted")) << "role change must drop refresh sessions";

    HttpResponsePtr r2;
    admin.deleteUser(
        authed(admin_user.principal), [&](const HttpResponsePtr& r) { r2 = r; }, deleted.user.id);
    ASSERT_EQ(r2->statusCode(), k200OK);
    EXPECT_FALSE(Security::Sessions::consume(cookies, "jti-deleted")) << "delete must drop refresh sessions";
}

TEST_F(AdminFlowTest, adminCannotDeleteSelf) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    HttpResponsePtr resp;
    admin.deleteUser(
        authed(admin_user.principal), [&](const HttpResponsePtr& r) { resp = r; }, admin_user.user.id);
    ASSERT_EQ(resp->statusCode(), k400BadRequest);
    auto body = json::parse(std::string(resp->body()));
    EXPECT_EQ(body["error"].get<std::string>(), "self_delete");
    Repositories::UserRepository repo;
    EXPECT_TRUE(repo.find(admin_user.user.id).has_value());
}

TEST_F(AdminFlowTest, adminCannotChangeOwnRole) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    Repositories::RoleRepository roles;
    auto user_role = roles.find_by_name("User");
    ASSERT_TRUE(user_role.has_value());

    auto req = authed_post(admin_user.principal, {{"role_id", user_role->id}});
    req->setMethod(Patch);
    HttpResponsePtr resp;
    admin.updateUser(
        req, [&](const HttpResponsePtr& r) { resp = r; }, admin_user.user.id);
    ASSERT_EQ(resp->statusCode(), k400BadRequest);
    auto body = json::parse(std::string(resp->body()));
    EXPECT_EQ(body["error"].get<std::string>(), "self_role_change");
}

TEST_F(AdminFlowTest, listRolesShowsBothSeededRoles) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    HttpResponsePtr resp;
    admin.listRoles(authed(admin_user.principal), [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_EQ(resp->statusCode(), k200OK);
    auto body = json::parse(std::string(resp->body()));
    auto names = std::vector<std::string>{};
    for (const auto& r : body["data"])
        names.push_back(r["name"].get<std::string>());
    EXPECT_NE(std::find(names.begin(), names.end(), "User"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "Administrator"), names.end());
}

// ── Roles CRUD ─────────────────────────────────────────────────────────────

TEST_F(AdminFlowTest, createRoleAsAdmin) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    auto req = authed_post(admin_user.principal, {{"name", "Editor"}, {"permissions", kEditorPerms}});
    HttpResponsePtr resp;
    admin.createRole(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k201Created);
    auto body = json::parse(std::string(resp->body()));
    EXPECT_EQ(body["data"]["name"].get<std::string>(), "Editor");
    EXPECT_EQ(body["data"]["permissions"].get<int>(), 3);
}

TEST_F(AdminFlowTest, createRoleRejectsDuplicateName) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    auto req = authed_post(admin_user.principal, {{"name", "User"}, {"permissions", 1}});
    HttpResponsePtr resp;
    admin.createRole(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_EQ(resp->statusCode(), k409Conflict);
    auto body = json::parse(std::string(resp->body()));
    EXPECT_EQ(body["error"].get<std::string>(), "role_exists");
}

TEST_F(AdminFlowTest, updateRolePatchesFields) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    Repositories::RoleRepository repo;
    auto created = repo.create("Editor", kEditorPerms, false);

    auto req = authed_post(admin_user.principal, {{"permissions", kEditorPermsWidened}});
    req->setMethod(Patch);
    HttpResponsePtr resp;
    admin.updateRole(
        req, [&](const HttpResponsePtr& r) { resp = r; }, std::to_string(created.id));
    ASSERT_EQ(resp->statusCode(), k200OK);
    auto body = json::parse(std::string(resp->body()));
    EXPECT_EQ(body["data"]["permissions"].get<int>(), 7);
    EXPECT_EQ(body["data"]["name"].get<std::string>(), "Editor");
}

TEST_F(AdminFlowTest, updateRoleEmptyPatchRefused) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    Repositories::RoleRepository repo;
    auto created = repo.create("Editor", kEditorPerms, false);
    auto req = authed_post(admin_user.principal, json::object());
    req->setMethod(Patch);
    HttpResponsePtr resp;
    admin.updateRole(
        req, [&](const HttpResponsePtr& r) { resp = r; }, std::to_string(created.id));
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
}

TEST_F(AdminFlowTest, createRoleRefusesADefaultRoleWithTheAdminBit) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    auto req =
        authed_post(admin_user.principal,
                    {{"name", "Everyone"}, {"permissions", Domain::Permission::kAdminister | 1}, {"is_default", true}});
    HttpResponsePtr resp;
    admin.createRole(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
    EXPECT_EQ(json::parse(std::string(resp->body()))["error"], "default_role_admin");
    EXPECT_FALSE(Repositories::RoleRepository().find_by_name("Everyone").has_value());
}

TEST_F(AdminFlowTest, updateRoleRefusesTheAdminBitOnTheDefaultRole) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    Repositories::RoleRepository repo;
    auto user_role = repo.find_by_name("User");
    ASSERT_TRUE(user_role.has_value());
    ASSERT_TRUE(user_role->is_default);

    auto req = authed_post(admin_user.principal, {{"permissions", Domain::Permission::kAdminister | 1}});
    req->setMethod(Patch);
    HttpResponsePtr resp;
    admin.updateRole(
        req, [&](const HttpResponsePtr& r) { resp = r; }, std::to_string(user_role->id));
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
    EXPECT_EQ(json::parse(std::string(resp->body()))["error"], "default_role_admin");
    EXPECT_EQ(repo.find(user_role->id)->permissions, user_role->permissions);
}

TEST_F(AdminFlowTest, updateRoleRefusesMakingAnAdminRoleTheDefault) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    auto req = authed_post(admin_user.principal, {{"is_default", true}});
    req->setMethod(Patch);
    HttpResponsePtr resp;
    admin.updateRole(
        req, [&](const HttpResponsePtr& r) { resp = r; }, std::to_string(admin_user.user.role_id));
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
    EXPECT_EQ(json::parse(std::string(resp->body()))["error"], "default_role_admin");
}

TEST_F(AdminFlowTest, updateRoleRefusesDroppingTheAdminBitFromTheLastAdminRole) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    Repositories::RoleRepository repo;
    // wipe_app_data keeps the seed roles' permissions as they are, so this
    // test must hand them back exactly, even when an assertion bails out.
    RestoreRolePermissions restore;
    // Make the seed Administrator role the only admin-bit role, whatever an
    // earlier suite left behind.
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("UPDATE roles SET permissions = permissions & ~$1::int WHERE id <> $2",
                        static_cast<int>(Domain::Permission::kAdminister),
                        admin_user.user.role_id);
        txn.exec_params("UPDATE roles SET permissions = permissions | $1::int WHERE id = $2",
                        static_cast<int>(Domain::Permission::kAdminister),
                        admin_user.user.role_id);
        return 0;
    });

    auto req = authed_post(admin_user.principal, {{"permissions", 1}});
    req->setMethod(Patch);
    HttpResponsePtr resp;
    admin.updateRole(
        req, [&](const HttpResponsePtr& r) { resp = r; }, std::to_string(admin_user.user.role_id));
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->statusCode(), k409Conflict);
    EXPECT_EQ(json::parse(std::string(resp->body()))["error"], "last_admin_role");
    EXPECT_TRUE(repo.find(admin_user.user.role_id)->is_admin());

    // With a second admin role the same demotion goes through.
    repo.create("Second admins", Domain::Permission::kAdminister, false);
    HttpResponsePtr ok;
    auto again = authed_post(admin_user.principal, {{"permissions", 1}});
    again->setMethod(Patch);
    admin.updateRole(
        again, [&](const HttpResponsePtr& r) { ok = r; }, std::to_string(admin_user.user.role_id));
    ASSERT_NE(ok, nullptr);
    EXPECT_EQ(ok->statusCode(), k200OK) << ok->body();
}

TEST_F(AdminFlowTest, deleteRoleHappyPath) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    Repositories::RoleRepository repo;
    auto created = repo.create("Editor", kEditorPerms, false);
    HttpResponsePtr resp;
    admin.deleteRole(
        authed(admin_user.principal), [&](const HttpResponsePtr& r) { resp = r; }, std::to_string(created.id));
    EXPECT_EQ(resp->statusCode(), k200OK);
    EXPECT_FALSE(repo.find(created.id).has_value());
}

TEST_F(AdminFlowTest, deleteRoleProtectsDefault) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    Repositories::RoleRepository repo;
    auto user_role = repo.find_by_name("User");
    ASSERT_TRUE(user_role.has_value());
    HttpResponsePtr resp;
    admin.deleteRole(
        authed(admin_user.principal), [&](const HttpResponsePtr& r) { resp = r; }, std::to_string(user_role->id));
    EXPECT_EQ(resp->statusCode(), k400BadRequest);
    auto body = json::parse(std::string(resp->body()));
    EXPECT_EQ(body["error"].get<std::string>(), "default_role_protected");
    EXPECT_TRUE(repo.find(user_role->id).has_value());
}

TEST_F(AdminFlowTest, deleteRoleRefusesIfReferenced) {
    auto admin_user = seed_user("admin@example.com", "Administrator");
    HttpResponsePtr resp;
    admin.deleteRole(
        authed(admin_user.principal),
        [&](const HttpResponsePtr& r) { resp = r; },
        std::to_string(admin_user.user.role_id));
    EXPECT_EQ(resp->statusCode(), k409Conflict);
    auto body = json::parse(std::string(resp->body()));
    EXPECT_EQ(body["error"].get<std::string>(), "role_in_use");
}

}  // namespace
