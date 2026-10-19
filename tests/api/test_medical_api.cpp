/**
 * @file test_medical_api.cpp
 * @brief /api/v1/medical: the module switch, the owner scope on every id and
 *        every reference, a person kept while something is on file, the
 *        record and medication filters, and the tasks of a follow-up date and
 *        of a medication end (created once, then the same one), and one
 *        visit, record or medication read by its id.
 */

#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/MedicalController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaa0d11";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbb0d12";

Security::Auth::AuthPrincipal principal(const char* who) {
    Security::Auth::AuthPrincipal p;
    p.subject = who;
    p.raw_claims = json{{"sub", who}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

using TestHelpers::body_of;
using Fn = std::function<void(const HttpRequestPtr&, Api::MedicalController::Callback&&)>;

void wipe() {
    Database::get().execute_write([](auto& txn) {
        for (const char* id : {kAnna, kBoris}) {
            const std::string o = id;
            for (const char* table : {"private_files",
                                      "medical_medications",
                                      "medical_records",
                                      "medical_visits",
                                      "medical_providers",
                                      "medical_people",
                                      "task_items"}) {
                txn.exec_params(std::string("DELETE FROM ") + table + " WHERE owner_id = $1::uuid", o);
            }
        }
        return true;
    });
}

class MedicalApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::MedicalController c;
    bool tasks_on = true;

    std::string config_file_name() const override { return "medical_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["medical"]["enabled"] = true;
        cfg["tasks"]["enabled"] = tasks_on;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        wipe();
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

    json person(const char* who, const char* name) {
        auto r = call(
            who,
            Post,
            json{{"name", name}, {"relation", "me"}},
            [&](auto req, auto cb) { c.createPerson(req, std::move(cb)); },
            "/api/v1/medical/people");
        EXPECT_EQ(r->statusCode(), k201Created) << std::string(r->body());
        return body_of(r)["data"];
    }

    json visit(const char* who, const json& body) {
        auto r = call(
            who, Post, body, [&](auto req, auto cb) { c.createVisit(req, std::move(cb)); }, "/api/v1/medical/visits");
        EXPECT_EQ(r->statusCode(), k201Created) << std::string(r->body());
        return body_of(r)["data"];
    }

    json record(const char* who, const json& body) {
        auto r = call(
            who, Post, body, [&](auto req, auto cb) { c.createRecord(req, std::move(cb)); }, "/api/v1/medical/records");
        EXPECT_EQ(r->statusCode(), k201Created) << std::string(r->body());
        return body_of(r)["data"];
    }

    json medication(const char* who, const json& body) {
        auto r = call(
            who,
            Post,
            body,
            [&](auto req, auto cb) { c.createMedication(req, std::move(cb)); },
            "/api/v1/medical/medications");
        EXPECT_EQ(r->statusCode(), k201Created) << std::string(r->body());
        return body_of(r)["data"];
    }
};

class MedicalOffTest : public TestHelpers::CoreBackedTest {
protected:
    Api::MedicalController c;
    std::string config_file_name() const override { return "medical_off_test_config.json"; }
};

class MedicalNoTasksTest : public MedicalApiTest {
protected:
    MedicalNoTasksTest() { tasks_on = false; }
    std::string config_file_name() const override { return "medical_no_tasks_test_config.json"; }
};

}  // namespace

TEST_F(MedicalOffTest, EveryRouteIs404WhileTheModuleIsOff) {
    auto req = TestHelpers::authed_json(principal(kAnna), json::object(), Get);
    HttpResponsePtr r;
    c.listPeople(req, [&](const HttpResponsePtr& x) { r = x; });
    ASSERT_TRUE(r);
    EXPECT_EQ(r->statusCode(), k404NotFound);
    EXPECT_EQ(body_of(r)["message"], "medical not found") << std::string(r->body());
    HttpResponsePtr t;
    c.visitTask(
        req, [&](const HttpResponsePtr& x) { t = x; }, "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    ASSERT_TRUE(t);
    EXPECT_EQ(t->statusCode(), k404NotFound);
}

TEST_F(MedicalApiTest, PeopleAndProvidersBelongToTheirOwner) {
    const json anna = person(kAnna, "Anna");
    auto mine = call(
        kAnna,
        Get,
        json::object(),
        [&](auto req, auto cb) { c.listPeople(req, std::move(cb)); },
        "/api/v1/medical/people");
    ASSERT_EQ(body_of(mine)["data"].size(), 1u);
    auto theirs = call(kBoris, Get, json::object(), [&](auto req, auto cb) { c.listPeople(req, std::move(cb)); });
    EXPECT_TRUE(body_of(theirs)["data"].empty());

    const std::string id = anna["id"].get<std::string>();
    auto patch = call(
        kBoris, Patch, json{{"name", "Mine now"}}, [&](auto req, auto cb) { c.updatePerson(req, std::move(cb), id); });
    EXPECT_EQ(patch->statusCode(), k404NotFound);
    auto del = call(kBoris, Delete, json::object(), [&](auto req, auto cb) { c.deletePerson(req, std::move(cb), id); });
    EXPECT_EQ(del->statusCode(), k404NotFound);

    auto doc = call(
        kAnna,
        Post,
        json{{"kind", "doctor"}, {"name", "Dr. Lee"}, {"specialty", "GP"}, {"email", "lee@clinic.example"}},
        [&](auto req, auto cb) { c.createProvider(req, std::move(cb)); },
        "/api/v1/medical/providers");
    ASSERT_EQ(doc->statusCode(), k201Created) << std::string(doc->body());
    auto bad =
        call(kAnna, Post, json{{"kind", "dentist"}, {"name", "X"}, {"email", "not-an-email"}}, [&](auto req, auto cb) {
            c.createProvider(req, std::move(cb));
        });
    EXPECT_EQ(bad->statusCode(), k400BadRequest);
}

TEST_F(MedicalApiTest, AReferenceToAnotherOwnersRowIsUnknown) {
    const json anna = person(kAnna, "Anna");
    const json boris = person(kBoris, "Boris");
    auto r = call(kAnna,
                  Post,
                  json{{"person_id", boris["id"]}, {"date", "2026-10-01"}, {"reason", "Checkup"}},
                  [&](auto req, auto cb) { c.createVisit(req, std::move(cb)); });
    ASSERT_EQ(r->statusCode(), k400BadRequest) << std::string(r->body());
    EXPECT_EQ(body_of(r)["error"], "unknown_reference");

    const json their_visit = visit(kBoris, json{{"person_id", boris["id"]}, {"date", "2026-10-01"}, {"reason", "Flu"}});
    auto rec = call(kAnna,
                    Post,
                    json{{"person_id", anna["id"]},
                         {"visit_id", their_visit["id"]},
                         {"date", "2026-10-02"},
                         {"kind", "lab"},
                         {"title", "Blood"}},
                    [&](auto req, auto cb) { c.createRecord(req, std::move(cb)); });
    EXPECT_EQ(rec->statusCode(), k400BadRequest) << std::string(rec->body());
    EXPECT_EQ(body_of(rec)["error"], "unknown_reference");
}

TEST_F(MedicalApiTest, VisitsAndRecordsListNewestFirstAndFilter) {
    const json anna = person(kAnna, "Anna");
    const json son = person(kAnna, "Timur");
    const json v = visit(kAnna,
                         json{{"person_id", anna["id"]},
                              {"date", "2026-09-01"},
                              {"reason", "Checkup"},
                              {"outcome", "All fine"},
                              {"follow_up", "2027-03-01"}});
    EXPECT_EQ(v["person_name"], "Anna");
    EXPECT_TRUE(v["provider_id"].is_null());
    visit(kAnna, json{{"person_id", son["id"]}, {"date", "2026-10-01"}, {"reason", "Cough"}});

    auto all = call(
        kAnna,
        Get,
        json::object(),
        [&](auto req, auto cb) { c.listVisits(req, std::move(cb)); },
        "/api/v1/medical/visits");
    ASSERT_EQ(body_of(all)["data"].size(), 2u);
    EXPECT_EQ(body_of(all)["data"][0]["reason"], "Cough");
    auto annas = call(kAnna,
                      Get,
                      json::object(),
                      [&](auto req, auto cb) { c.listVisits(req, std::move(cb)); },
                      nullptr,
                      {{"person", anna["id"].get<std::string>()}});
    ASSERT_EQ(body_of(annas)["data"].size(), 1u);
    auto bad = call(kAnna,
                    Get,
                    json::object(),
                    [&](auto req, auto cb) { c.listVisits(req, std::move(cb)); },
                    nullptr,
                    {{"person", "nope"}});
    EXPECT_EQ(bad->statusCode(), k400BadRequest);

    const json rec = record(kAnna,
                            json{{"person_id", anna["id"]},
                                 {"visit_id", v["id"]},
                                 {"date", "2026-09-02"},
                                 {"kind", "lab"},
                                 {"title", "Blood"}});
    EXPECT_EQ(rec["files"], 0);
    record(kAnna,
           json{{"person_id", son["id"]}, {"date", "2026-10-02"}, {"kind", "vaccination"}, {"title", "Flu shot"}});
    auto labs = call(kAnna,
                     Get,
                     json::object(),
                     [&](auto req, auto cb) { c.listRecords(req, std::move(cb)); },
                     "/api/v1/medical/records",
                     {{"kind", "lab"}});
    ASSERT_EQ(body_of(labs)["data"].size(), 1u);
    EXPECT_EQ(body_of(labs)["data"][0]["title"], "Blood");
    auto badkind = call(kAnna,
                        Get,
                        json::object(),
                        [&](auto req, auto cb) { c.listRecords(req, std::move(cb)); },
                        nullptr,
                        {{"kind", "diagnosis"}});
    EXPECT_EQ(badkind->statusCode(), k400BadRequest);

    // A visit removed leaves its record, unlinked.
    const std::string vid = v["id"].get<std::string>();
    auto gone = call(kAnna, Delete, json::object(), [&](auto req, auto cb) { c.deleteVisit(req, std::move(cb), vid); });
    EXPECT_EQ(gone->statusCode(), k204NoContent);
    auto after = call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.listRecords(req, std::move(cb)); });
    const json records = body_of(after)["data"];  // a named copy: the loop must not walk a temporary
    ASSERT_EQ(records.size(), 2u);
    for (const auto& r : records) {
        EXPECT_TRUE(r["visit_id"].is_null());
    }
}

TEST_F(MedicalApiTest, APersonWithSomethingOnFileStays) {
    const json anna = person(kAnna, "Anna");
    const std::string id = anna["id"].get<std::string>();
    const json m = medication(kAnna, json{{"person_id", anna["id"]}, {"name", "Vitamin D"}, {"dose", "1000 IU"}});
    auto kept = call(kAnna, Delete, json::object(), [&](auto req, auto cb) { c.deletePerson(req, std::move(cb), id); });
    ASSERT_EQ(kept->statusCode(), k409Conflict) << std::string(kept->body());
    EXPECT_EQ(body_of(kept)["error"], "person_in_use");
    const std::string mid = m["id"].get<std::string>();
    call(kAnna, Delete, json::object(), [&](auto req, auto cb) { c.deleteMedication(req, std::move(cb), mid); });
    auto gone = call(kAnna, Delete, json::object(), [&](auto req, auto cb) { c.deletePerson(req, std::move(cb), id); });
    EXPECT_EQ(gone->statusCode(), k204NoContent);
}

TEST_F(MedicalApiTest, MedicationsListTheCurrentOnesFirst) {
    const json anna = person(kAnna, "Anna");
    medication(kAnna,
               json{{"person_id", anna["id"]},
                    {"name", "Antibiotic"},
                    {"start_date", "2020-01-01"},
                    {"end_date", "2020-01-10"}});
    medication(kAnna, json{{"person_id", anna["id"]}, {"name", "Vitamin D"}, {"schedule", "every morning"}});
    auto bad =
        call(kAnna,
             Post,
             json{{"person_id", anna["id"]}, {"name", "X"}, {"start_date", "2026-02-01"}, {"end_date", "2026-01-01"}},
             [&](auto req, auto cb) { c.createMedication(req, std::move(cb)); });
    EXPECT_EQ(bad->statusCode(), k400BadRequest);

    auto all = call(
        kAnna,
        Get,
        json::object(),
        [&](auto req, auto cb) { c.listMedications(req, std::move(cb)); },
        "/api/v1/medical/medications");
    ASSERT_EQ(body_of(all)["data"].size(), 2u);
    EXPECT_EQ(body_of(all)["data"][0]["name"], "Vitamin D");
    auto current = call(kAnna,
                        Get,
                        json::object(),
                        [&](auto req, auto cb) { c.listMedications(req, std::move(cb)); },
                        nullptr,
                        {{"current", "true"}});
    ASSERT_EQ(body_of(current)["data"].size(), 1u);
    EXPECT_EQ(body_of(current)["data"][0]["name"], "Vitamin D");
}

TEST_F(MedicalApiTest, AFollowUpDateGetsOneTask) {
    const json anna = person(kAnna, "Anna");
    const json none = visit(kAnna, json{{"person_id", anna["id"]}, {"date", "2026-09-01"}, {"reason", "Checkup"}});
    const std::string none_id = none["id"].get<std::string>();
    auto no_date =
        call(kAnna, Post, json::object(), [&](auto req, auto cb) { c.visitTask(req, std::move(cb), none_id); });
    ASSERT_EQ(no_date->statusCode(), k409Conflict) << std::string(no_date->body());
    EXPECT_EQ(body_of(no_date)["error"], "no_date");

    const json v = visit(
        kAnna,
        json{{"person_id", anna["id"]}, {"date", "2026-09-01"}, {"reason", "Thyroid"}, {"follow_up", "2027-03-01"}});
    const std::string id = v["id"].get<std::string>();
    auto first = call(
        kAnna,
        Post,
        json::object(),
        [&](auto req, auto cb) { c.visitTask(req, std::move(cb), id); },
        "/api/v1/medical/visits/{id}/task");
    ASSERT_EQ(first->statusCode(), k201Created) << std::string(first->body());
    const json task = body_of(first)["data"]["task"];
    EXPECT_EQ(task["due"], "2027-03-01");
    EXPECT_EQ(task["title"], "Anna: follow-up, Thyroid");
    EXPECT_EQ(body_of(first)["data"]["visit"]["follow_up_task_id"], task["id"]);

    auto again = call(
        kAnna,
        Post,
        json::object(),
        [&](auto req, auto cb) { c.visitTask(req, std::move(cb), id); },
        "/api/v1/medical/visits/{id}/task");
    ASSERT_EQ(again->statusCode(), k200OK);
    EXPECT_EQ(body_of(again)["data"]["task"]["id"], task["id"]);
    EXPECT_FALSE(body_of(again)["data"]["created"].get<bool>());

    const long area = Database::get().execute_read([&](auto& txn) {
        return txn
            .exec_params("SELECT count(*) FROM task_items WHERE owner_id = $1::uuid AND area = 'health'",
                         std::string(kAnna))[0][0]
            .template as<long>();
    });
    EXPECT_EQ(area, 1);

    auto theirs = call(kBoris, Post, json::object(), [&](auto req, auto cb) { c.visitTask(req, std::move(cb), id); });
    EXPECT_EQ(theirs->statusCode(), k404NotFound);
}

TEST_F(MedicalApiTest, AMedicationEndGetsOneTask) {
    const json anna = person(kAnna, "Anna");
    const json m = medication(kAnna, json{{"person_id", anna["id"]}, {"name", "Iron"}, {"end_date", "2026-12-01"}});
    const std::string id = m["id"].get<std::string>();
    auto first = call(
        kAnna,
        Post,
        json::object(),
        [&](auto req, auto cb) { c.medicationTask(req, std::move(cb), id); },
        "/api/v1/medical/medications/{id}/task");
    ASSERT_EQ(first->statusCode(), k201Created) << std::string(first->body());
    EXPECT_EQ(body_of(first)["data"]["task"]["due"], "2026-12-01");
    EXPECT_EQ(body_of(first)["data"]["medication"]["end_task_id"], body_of(first)["data"]["task"]["id"]);
    auto again =
        call(kAnna, Post, json::object(), [&](auto req, auto cb) { c.medicationTask(req, std::move(cb), id); });
    EXPECT_EQ(again->statusCode(), k200OK);
}

TEST_F(MedicalNoTasksTest, TheTaskRouteIs404WhileTasksIsOff) {
    const json anna = person(kAnna, "Anna");
    const json v = visit(
        kAnna,
        json{{"person_id", anna["id"]}, {"date", "2026-09-01"}, {"reason", "Thyroid"}, {"follow_up", "2027-03-01"}});
    const std::string id = v["id"].get<std::string>();
    auto r = call(kAnna, Post, json::object(), [&](auto req, auto cb) { c.visitTask(req, std::move(cb), id); });
    ASSERT_EQ(r->statusCode(), k404NotFound);
    EXPECT_EQ(body_of(r)["message"], "tasks not found");
}

TEST_F(MedicalApiTest, OneRowReadsLikeAnElementOfItsList) {
    const json anna = person(kAnna, "Anna");
    const json v = visit(kAnna, json{{"person_id", anna["id"]}, {"date", "2026-09-01"}, {"reason", "Thyroid"}});
    const json r = record(kAnna,
                          json{{"person_id", anna["id"]},
                               {"visit_id", v["id"]},
                               {"date", "2026-09-01"},
                               {"kind", "lab"},
                               {"title", "TSH"}});
    const json m = medication(kAnna, json{{"person_id", anna["id"]}, {"name", "Vitamin D"}, {"dose", "1000 IU"}});
    const std::string vid = v["id"].get<std::string>();
    const std::string rid = r["id"].get<std::string>();
    const std::string mid = m["id"].get<std::string>();

    struct Kind {
        const char* name;
        const char* spec_path;
        std::string id;
        Fn get;
        Fn list;
    };
    const std::vector<Kind> kinds = {
        {"visit",
         "/api/v1/medical/visits/{id}",
         vid,
         [&](auto req, auto cb) { c.getVisit(req, std::move(cb), vid); },
         [&](auto req, auto cb) { c.listVisits(req, std::move(cb)); }},
        {"record",
         "/api/v1/medical/records/{id}",
         rid,
         [&](auto req, auto cb) { c.getRecord(req, std::move(cb), rid); },
         [&](auto req, auto cb) { c.listRecords(req, std::move(cb)); }},
        {"medication",
         "/api/v1/medical/medications/{id}",
         mid,
         [&](auto req, auto cb) { c.getMedication(req, std::move(cb), mid); },
         [&](auto req, auto cb) { c.listMedications(req, std::move(cb)); }},
    };
    for (const auto& k : kinds) {
        auto own = call(kAnna, Get, json::object(), k.get, k.spec_path);
        ASSERT_EQ(own->statusCode(), k200OK) << k.name << ": " << std::string(own->body());
        const json row = body_of(own)["data"];
        auto listed = call(kAnna, Get, json::object(), k.list);
        ASSERT_EQ(listed->statusCode(), k200OK) << k.name;
        const json rows = body_of(listed)["data"];
        ASSERT_EQ(rows.size(), 1U) << k.name;
        EXPECT_EQ(row, rows[0]) << k.name << ": the same fields as the list";
        EXPECT_EQ(row["id"], k.id) << k.name;

        auto foreign = call(kBoris, Get, json::object(), k.get);
        ASSERT_EQ(foreign->statusCode(), k404NotFound) << k.name << ": another owner's row";
        EXPECT_EQ(body_of(foreign)["error"], "not_found") << k.name;
    }
    EXPECT_EQ(v["reason"], "Thyroid");
    EXPECT_EQ(r["title"], "TSH");
    EXPECT_EQ(m["dose"], "1000 IU");

    const std::string random = "0d0d0d0d-1111-4222-8333-444455556666";
    const std::vector<Fn> missing_gets = {
        [&](auto req, auto cb) { c.getVisit(req, std::move(cb), random); },
        [&](auto req, auto cb) { c.getRecord(req, std::move(cb), random); },
        [&](auto req, auto cb) { c.getMedication(req, std::move(cb), random); },
    };
    for (const Fn& get : missing_gets) {
        auto missing = call(kAnna, Get, json::object(), get);
        ASSERT_EQ(missing->statusCode(), k404NotFound) << std::string(missing->body());
        EXPECT_EQ(body_of(missing)["error"], "not_found");
    }
    auto bad = call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.getVisit(req, std::move(cb), "nope"); });
    EXPECT_EQ(bad->statusCode(), k400BadRequest);
}
