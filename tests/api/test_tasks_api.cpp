/**
 * @file test_tasks_api.cpp
 * @brief Tasks module routes: two users apart, completed_at owned by the
 *        server, the source pair, the agenda read in the caller's zone, the
 *        inbox, the one-phrase parse, the module switch.
 */

#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/TasksController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/tasks/ParseJobRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaab2";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbc2";

Security::Auth::AuthPrincipal user(const std::string& id) {
    Security::Auth::AuthPrincipal p;
    p.subject = id;
    p.raw_claims = json{{"sub", id}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

json body_of(const HttpResponsePtr& resp) {
    return json::parse(std::string(resp->body()));
}

class TasksApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::TasksController controller;

    std::string config_file_name() const override { return "tasks_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["tasks"]["enabled"] = true;
        cfg["money"]["enabled"] = true;  // the ledger-row source check runs only with money on
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec(
                "TRUNCATE TABLE task_parse_jobs, task_notes, task_items, money_transactions, money_accounts, "
                "money_categories, money_currencies");
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

    using Handler = void (Api::TasksController::*)(const HttpRequestPtr&, Api::TasksController::Callback&&);
    using IdHandler = void (Api::TasksController::*)(const HttpRequestPtr&,
                                                     Api::TasksController::Callback&&,
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

    json created(Handler h, const std::string& who, const json& body) {
        const auto resp = call_json(h, user(who), body);
        EXPECT_EQ(resp->statusCode(), k201Created) << resp->body();
        return body_of(resp)["data"];
    }
};

class TasksOffTest : public TasksApiTest {
protected:
    std::string config_file_name() const override { return "tasks_off_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["tasks"]["enabled"] = false; }
};

}  // namespace

TEST_F(TasksApiTest, ATaskBelongsToItsOwnerAndTheServerOwnsCompletedAt) {
    const json t = created(&Api::TasksController::createItem,
                           kAnna,
                           json{{"title", "Renew the insurance"},
                                {"area", "finance"},
                                {"due", "2026-10-20"},
                                {"completed_at", "2020-01-01T00:00:00+00:00"}});
    EXPECT_TRUE(t["completed_at"].is_null()) << "a body's completed_at is ignored";
    EXPECT_EQ(call_id(&Api::TasksController::getItem, user(kBoris), Get, t["id"])->statusCode(), k404NotFound);
    const auto done = call_id(&Api::TasksController::updateItem, user(kAnna), Patch, t["id"], json{{"status", "done"}});
    ASSERT_EQ(done->statusCode(), k200OK) << done->body();
    EXPECT_TRUE(body_of(done)["data"]["completed_at"].is_string());
    const auto again =
        call_id(&Api::TasksController::updateItem, user(kAnna), Patch, t["id"], json{{"status", "open"}});
    EXPECT_TRUE(body_of(again)["data"]["completed_at"].is_null());
    EXPECT_EQ(
        call_json(&Api::TasksController::createItem, user(kAnna), json{{"title", "x"}, {"area", "work"}})->statusCode(),
        k400BadRequest)
        << "work is not an area";
    EXPECT_EQ(call_json(&Api::TasksController::createItem, user(kAnna), json{{"title", ""}, {"area", "health"}})
                  ->statusCode(),
              k400BadRequest);
    EXPECT_EQ(call_id(&Api::TasksController::deleteItem, user(kAnna), Delete, t["id"])->statusCode(), k204NoContent);
}

TEST_F(TasksApiTest, ASourceIsAPairAndALedgerRowMustBeYours) {
    const auto post = [&](const json& b) {
        return call_json(&Api::TasksController::createItem, user(kAnna), b)->statusCode();
    };
    const json base{{"title", "Dispute the charge"}, {"area", "finance"}};
    json kind_only = base;
    kind_only["source_kind"] = "url";
    EXPECT_EQ(post(kind_only), k400BadRequest);
    json url = base;
    url["source_kind"] = "url";
    url["source_ref"] = "ftp://x";
    EXPECT_EQ(post(url), k400BadRequest) << "a link is http(s)";
    url["source_ref"] = "https://mail.example/1";
    EXPECT_EQ(post(url), k201Created);
    // Boris's ledger row as Anna's source: refused.
    const std::string boris_row = Database::get().execute_write([](auto& txn) {
        txn.exec_params(
            "INSERT INTO money_currencies (owner_id, code, name, decimals) VALUES ($1::uuid, 'KZT', 't', 2)",
            std::string(kBoris));
        const auto acc = txn.exec_params(
                                "INSERT INTO money_accounts (owner_id, name, currency) VALUES ($1::uuid, 'K', 'KZT') "
                                "RETURNING id::text",
                                std::string(kBoris))[0][0]
                             .template as<std::string>();
        const auto cat = txn.exec_params(
                                "INSERT INTO money_categories (owner_id, name, kind) VALUES ($1::uuid, 'Food', "
                                "'expense') RETURNING id::text",
                                std::string(kBoris))[0][0]
                             .template as<std::string>();
        return txn
            .exec_params(
                "INSERT INTO money_transactions (owner_id, type, date, account_id, amount, category_id, name) "
                "VALUES ($1::uuid, 'expense', '2026-10-05', $2::uuid, 2790, $3::uuid, 'x') RETURNING id::text",
                std::string(kBoris),
                acc,
                cat)[0][0]
            .template as<std::string>();
    });
    json ledger = base;
    ledger["source_kind"] = "money_transaction";
    ledger["source_ref"] = boris_row;
    EXPECT_EQ(post(ledger), k400BadRequest) << "another user's ledger row";
}

TEST_F(TasksApiTest, TheAgendaReadsTheDayInTheGivenZone) {
    created(
        &Api::TasksController::createItem, kAnna, json{{"title", "Late"}, {"area", "health"}, {"due", "2026-10-06"}});
    created(
        &Api::TasksController::createItem, kAnna, json{{"title", "Edge"}, {"area", "travel"}, {"due", "2026-10-09"}});
    created(
        &Api::TasksController::createItem, kAnna, json{{"title", "After"}, {"area", "travel"}, {"due", "2026-10-10"}});
    created(&Api::TasksController::createItem, kAnna, json{{"title", "Someday"}, {"area", "growth"}});
    const json closed =
        created(&Api::TasksController::createItem, kAnna, json{{"title", "Closed"}, {"area", "projects"}});
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params(
            "UPDATE task_items SET status = 'done', completed_at = '2026-10-06 23:30:00+00' WHERE id = $1::uuid",
            closed["id"].get<std::string>());
        return true;
    });
    const auto get = [&](const std::string& tz) {
        return call(&Api::TasksController::agenda, user(kAnna), Get, {{"date", "2026-10-07"}, {"tz", tz}});
    };
    const json bkk = body_of(get("Asia/Bangkok"))["data"];
    EXPECT_EQ(bkk["late"].size(), 1u);
    EXPECT_EQ(bkk["soon"].size(), 1u);
    EXPECT_EQ(bkk["soon"][0]["title"], "Edge");
    EXPECT_EQ(bkk["dated"].size(), 1u);
    EXPECT_EQ(bkk["someday"].size(), 1u);
    EXPECT_EQ(bkk["done_today"].size(), 1u) << "23:30 UTC on the 6th is the 7th in Bangkok";
    EXPECT_EQ(body_of(get("UTC"))["data"]["done_today"].size(), 0u);
    EXPECT_EQ(get("Mars/Olympus")->statusCode(), k400BadRequest);
    EXPECT_EQ(call(&Api::TasksController::agenda, user(kAnna), Get, {{"date", "2026-02-30"}})->statusCode(),
              k400BadRequest);
}

TEST_F(TasksApiTest, NotesAreTheInbox) {
    const json n = created(&Api::TasksController::createNote, kAnna, json{{"text", "Coworking day passes"}});
    EXPECT_EQ(n["status"], "inbox");
    EXPECT_EQ(body_of(call(&Api::TasksController::listNotes, user(kAnna), Get))["data"].size(), 1u);
    EXPECT_EQ(body_of(call(&Api::TasksController::listNotes, user(kBoris), Get))["data"].size(), 0u);
    const auto archived =
        call_id(&Api::TasksController::updateNote, user(kAnna), Patch, n["id"], json{{"status", "archived"}});
    EXPECT_EQ(body_of(archived)["data"]["status"], "archived");
    EXPECT_EQ(body_of(call(&Api::TasksController::listNotes, user(kAnna), Get))["data"].size(), 0u);
}

TEST_F(TasksOffTest, EveryRouteIs404WhileTheModuleIsOff) {
    EXPECT_EQ(call(&Api::TasksController::status, user(kAnna), Get)->statusCode(), k404NotFound);
    EXPECT_EQ(call(&Api::TasksController::agenda, user(kAnna), Get)->statusCode(), k404NotFound);
}
