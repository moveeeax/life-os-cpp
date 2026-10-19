/**
 * @file MedicalController.cpp
 * @brief The /api/v1/medical handlers: validation, the owner scope, and the
 *        tasks of follow-up and end dates (MedicalController.hpp).
 */

#include "api/MedicalController.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <drogon/drogon.h>

#include <nlohmann/json.hpp>

#include "api/FieldChecks.hpp"
#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "api/Validation.hpp"
#include "repositories/Zones.hpp"
#include "repositories/medical/MedicalRepository.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Api::Fields::date_field;
using Api::Fields::text_length;
using Repositories::Medical::MedicationRepository;
using Repositories::Medical::PersonRepository;
using Repositories::Medical::ProviderRepository;
using Repositories::Medical::RecordRepository;
using Repositories::Medical::VisitRepository;
using Callback = MedicalController::Callback;

const std::vector<std::string> kProviderKinds = {"doctor", "clinic"};
const std::vector<std::string> kRecordKinds = {
    "lab", "imaging", "prescription", "discharge", "vaccination", "certificate", "other"};

std::string text(const json& body, const char* field) {
    return Validation::opt_string(body, field).value_or("");
}

/// A string field or null: nullopt for null or absent.
std::optional<std::string> nullable(const json& body, const char* field) {
    return Validation::opt_string(body, field);
}

/// An optional uuid field: null or absent is unset.
void opt_uuid(Validation::Errors& errs, const json& body, const char* field) {
    Validation::uuid(errs, body, field);
}

bool answer_errors(const Validation::Errors& errs, const Callback& callback) {
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return true;
    }
    return false;
}

/// ?person= as given (a uuid) or ""; nullopt (answered) when it is not a uuid.
std::optional<std::string> person_filter(const HttpRequestPtr& req, const Callback& callback) {
    const std::string person = req->getParameter("person");
    if (!person.empty() && !is_valid_uuid(person)) {
        callback(ErrorResponse::bad_request("invalid_query", "person must be a uuid"));
        return std::nullopt;
    }
    return person;
}

std::optional<PersonRepository::Input> person_input(const json& body, const Callback& callback) {
    Validation::Errors errs;
    Validation::require_string(errs, body, "name");
    text_length(errs, body, "name", 1, 80);
    text_length(errs, body, "relation", 0, 40);
    text_length(errs, body, "note", 0, 1000);
    if (answer_errors(errs, callback)) {
        return std::nullopt;
    }
    return PersonRepository::Input{body["name"].get<std::string>(), text(body, "relation"), text(body, "note")};
}

std::optional<ProviderRepository::Input> provider_input(const json& body, const Callback& callback) {
    Validation::Errors errs;
    Validation::require_string(errs, body, "kind");
    Validation::require_string(errs, body, "name");
    Validation::one_of(errs, body, "kind", kProviderKinds);
    text_length(errs, body, "name", 1, 120);
    text_length(errs, body, "specialty", 0, 80);
    text_length(errs, body, "clinic", 0, 120);
    text_length(errs, body, "phone", 0, 40);
    text_length(errs, body, "address", 0, 300);
    text_length(errs, body, "note", 0, 1000);
    if (!text(body, "email").empty()) {
        Validation::email(errs, body, "email");
    } else {
        text_length(errs, body, "email", 0, 254);
    }
    if (answer_errors(errs, callback)) {
        return std::nullopt;
    }
    return ProviderRepository::Input{body["kind"].get<std::string>(),
                                     body["name"].get<std::string>(),
                                     text(body, "specialty"),
                                     text(body, "clinic"),
                                     text(body, "phone"),
                                     text(body, "email"),
                                     text(body, "address"),
                                     text(body, "note")};
}

std::optional<VisitRepository::Input> visit_input(const json& body, const Callback& callback) {
    Validation::Errors errs;
    Validation::require_string(errs, body, "person_id");
    Validation::require_string(errs, body, "date");
    Validation::require_string(errs, body, "reason");
    Validation::uuid(errs, body, "person_id");
    opt_uuid(errs, body, "provider_id");
    date_field(errs, body, "date");
    date_field(errs, body, "follow_up");
    text_length(errs, body, "reason", 1, 200);
    text_length(errs, body, "outcome", 0, 2000);
    if (answer_errors(errs, callback)) {
        return std::nullopt;
    }
    return VisitRepository::Input{body["person_id"].get<std::string>(),
                                  nullable(body, "provider_id"),
                                  body["date"].get<std::string>(),
                                  body["reason"].get<std::string>(),
                                  text(body, "outcome"),
                                  nullable(body, "follow_up")};
}

std::optional<RecordRepository::Input> record_input(const json& body, const Callback& callback) {
    Validation::Errors errs;
    for (const char* f : {"person_id", "date", "kind", "title"}) {
        Validation::require_string(errs, body, f);
    }
    Validation::uuid(errs, body, "person_id");
    opt_uuid(errs, body, "visit_id");
    date_field(errs, body, "date");
    Validation::one_of(errs, body, "kind", kRecordKinds);
    text_length(errs, body, "title", 1, 200);
    text_length(errs, body, "note", 0, 2000);
    if (answer_errors(errs, callback)) {
        return std::nullopt;
    }
    return RecordRepository::Input{body["person_id"].get<std::string>(),
                                   nullable(body, "visit_id"),
                                   body["date"].get<std::string>(),
                                   body["kind"].get<std::string>(),
                                   body["title"].get<std::string>(),
                                   text(body, "note")};
}

std::optional<MedicationRepository::Input> medication_input(const json& body, const Callback& callback) {
    Validation::Errors errs;
    Validation::require_string(errs, body, "person_id");
    Validation::require_string(errs, body, "name");
    Validation::uuid(errs, body, "person_id");
    text_length(errs, body, "name", 1, 120);
    text_length(errs, body, "dose", 0, 80);
    text_length(errs, body, "schedule", 0, 200);
    text_length(errs, body, "note", 0, 1000);
    date_field(errs, body, "start_date");
    date_field(errs, body, "end_date");
    const auto start = nullable(body, "start_date");
    const auto end = nullable(body, "end_date");
    if (!errs.any() && start && end && *end < *start) {
        errs.add("end_date", "before_start", "the end is before the start");
    }
    if (answer_errors(errs, callback)) {
        return std::nullopt;
    }
    return MedicationRepository::Input{body["person_id"].get<std::string>(),
                                       body["name"].get<std::string>(),
                                       text(body, "dose"),
                                       text(body, "schedule"),
                                       start,
                                       end,
                                       text(body, "note")};
}

/// Parses the body and the input; nullopt (answered) when either is bad.
template <typename Parse>
auto body_input(const HttpRequestPtr& req, Callback& callback, Parse&& parse) -> decltype(parse(json{}, callback)) {
    json body;
    if (!parse_object(req, body, callback)) {
        return std::nullopt;
    }
    return parse(body, callback);
}

}  // namespace

#define MEDICAL_GUARD(req, callback, owner) \
    API_MODULE_USER_GUARD(Core::medical_enabled(), "medical", req, callback, owner)

// ── people ──────────────────────────────────────────────────────────────────

void MedicalController::listPeople(const HttpRequestPtr& req, Callback&& callback) {
    MEDICAL_GUARD(req, callback, owner);
    with_repo_errors(callback, "medical.people", [&] { callback(Response::list(PersonRepository().list(owner))); });
}

void MedicalController::createPerson(const HttpRequestPtr& req, Callback&& callback) {
    MEDICAL_GUARD(req, callback, owner);
    const auto in = body_input(req, callback, person_input);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "medical.people.create", [&] {
        callback(Response::created(json{{"data", PersonRepository().create(owner, *in)}}));
    });
}

void MedicalController::updatePerson(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MEDICAL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    const auto in = body_input(req, callback, person_input);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "medical.people.update", [&] {
        callback(Response::ok(json{{"data", PersonRepository().replace(owner, id, *in)}}));
    });
}

void MedicalController::deletePerson(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MEDICAL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "medical.people.delete", [&] {
        PersonRepository().remove(owner, id);
        no_content(callback);
    });
}

// ── doctors and clinics ─────────────────────────────────────────────────────

void MedicalController::listProviders(const HttpRequestPtr& req, Callback&& callback) {
    MEDICAL_GUARD(req, callback, owner);
    with_repo_errors(
        callback, "medical.providers", [&] { callback(Response::list(ProviderRepository().list(owner))); });
}

void MedicalController::createProvider(const HttpRequestPtr& req, Callback&& callback) {
    MEDICAL_GUARD(req, callback, owner);
    const auto in = body_input(req, callback, provider_input);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "medical.providers.create", [&] {
        callback(Response::created(json{{"data", ProviderRepository().create(owner, *in)}}));
    });
}

void MedicalController::updateProvider(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MEDICAL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    const auto in = body_input(req, callback, provider_input);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "medical.providers.update", [&] {
        callback(Response::ok(json{{"data", ProviderRepository().replace(owner, id, *in)}}));
    });
}

void MedicalController::deleteProvider(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MEDICAL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "medical.providers.delete", [&] {
        ProviderRepository().remove(owner, id);
        no_content(callback);
    });
}

// ── visits ──────────────────────────────────────────────────────────────────

void MedicalController::listVisits(const HttpRequestPtr& req, Callback&& callback) {
    MEDICAL_GUARD(req, callback, owner);
    const auto person = person_filter(req, callback);
    if (!person) {
        return;
    }
    with_repo_errors(
        callback, "medical.visits", [&] { callback(Response::list(VisitRepository().list(owner, *person))); });
}

void MedicalController::createVisit(const HttpRequestPtr& req, Callback&& callback) {
    MEDICAL_GUARD(req, callback, owner);
    const auto in = body_input(req, callback, visit_input);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "medical.visits.create", [&] {
        callback(Response::created(json{{"data", VisitRepository().create(owner, *in)}}));
    });
}

void MedicalController::getVisit(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MEDICAL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "medical.visits.get", [&] {
        const auto row = VisitRepository().get(owner, id);
        if (!row) {
            throw Repositories::Medical::NotFound("visit");
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

void MedicalController::updateVisit(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MEDICAL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    const auto in = body_input(req, callback, visit_input);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "medical.visits.update", [&] {
        callback(Response::ok(json{{"data", VisitRepository().replace(owner, id, *in)}}));
    });
}

void MedicalController::deleteVisit(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MEDICAL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "medical.visits.delete", [&] {
        VisitRepository().remove(owner, id);
        no_content(callback);
    });
}

void MedicalController::visitTask(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MEDICAL_GUARD(req, callback, owner);
    API_REQUIRE_MODULE(Core::tasks_enabled(), "tasks", callback);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "medical.visits.task", [&] {
        const auto a = VisitRepository().follow_up_task(owner, id);
        const json body{{"data", {{"visit", a.visit}, {"task", a.task}, {"created", a.created}}}};
        callback(a.created ? Response::created(body) : Response::ok(body));
    });
}

// ── records ─────────────────────────────────────────────────────────────────

void MedicalController::listRecords(const HttpRequestPtr& req, Callback&& callback) {
    MEDICAL_GUARD(req, callback, owner);
    const auto person = person_filter(req, callback);
    if (!person) {
        return;
    }
    const std::string kind = req->getParameter("kind");
    if (!kind.empty() && std::find(kRecordKinds.begin(), kRecordKinds.end(), kind) == kRecordKinds.end()) {
        callback(ErrorResponse::bad_request("invalid_query", "kind is not a record kind"));
        return;
    }
    with_repo_errors(
        callback, "medical.records", [&] { callback(Response::list(RecordRepository().list(owner, *person, kind))); });
}

void MedicalController::createRecord(const HttpRequestPtr& req, Callback&& callback) {
    MEDICAL_GUARD(req, callback, owner);
    const auto in = body_input(req, callback, record_input);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "medical.records.create", [&] {
        callback(Response::created(json{{"data", RecordRepository().create(owner, *in)}}));
    });
}

void MedicalController::getRecord(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MEDICAL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "medical.records.get", [&] {
        const auto row = RecordRepository().get(owner, id);
        if (!row) {
            throw Repositories::Medical::NotFound("record");
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

void MedicalController::updateRecord(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MEDICAL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    const auto in = body_input(req, callback, record_input);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "medical.records.update", [&] {
        callback(Response::ok(json{{"data", RecordRepository().replace(owner, id, *in)}}));
    });
}

void MedicalController::deleteRecord(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MEDICAL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "medical.records.delete", [&] {
        RecordRepository().remove(owner, id);
        no_content(callback);
    });
}

// ── medications ─────────────────────────────────────────────────────────────

void MedicalController::listMedications(const HttpRequestPtr& req, Callback&& callback) {
    MEDICAL_GUARD(req, callback, owner);
    const auto person = person_filter(req, callback);
    if (!person) {
        return;
    }
    const std::string current = req->getParameter("current");
    if (!current.empty() && current != "true" && current != "false") {
        callback(ErrorResponse::bad_request("invalid_query", "current must be true or false"));
        return;
    }
    const std::string today = local_today(req, &Repositories::Zones::today_in, Fields::today_utc());
    with_repo_errors(callback, "medical.medications", [&] {
        callback(Response::list(MedicationRepository().list(owner, *person, today, current == "true")));
    });
}

void MedicalController::createMedication(const HttpRequestPtr& req, Callback&& callback) {
    MEDICAL_GUARD(req, callback, owner);
    const auto in = body_input(req, callback, medication_input);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "medical.medications.create", [&] {
        callback(Response::created(json{{"data", MedicationRepository().create(owner, *in)}}));
    });
}

void MedicalController::getMedication(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MEDICAL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "medical.medications.get", [&] {
        const auto row = MedicationRepository().get(owner, id);
        if (!row) {
            throw Repositories::Medical::NotFound("medication");
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

void MedicalController::updateMedication(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MEDICAL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    const auto in = body_input(req, callback, medication_input);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "medical.medications.update", [&] {
        callback(Response::ok(json{{"data", MedicationRepository().replace(owner, id, *in)}}));
    });
}

void MedicalController::deleteMedication(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MEDICAL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "medical.medications.delete", [&] {
        MedicationRepository().remove(owner, id);
        no_content(callback);
    });
}

void MedicalController::medicationTask(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MEDICAL_GUARD(req, callback, owner);
    API_REQUIRE_MODULE(Core::tasks_enabled(), "tasks", callback);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "medical.medications.task", [&] {
        const auto a = MedicationRepository().end_task(owner, id);
        const json body{{"data", {{"medication", a.medication}, {"task", a.task}, {"created", a.created}}}};
        callback(a.created ? Response::created(body) : Response::ok(body));
    });
}

#undef MEDICAL_GUARD

}  // namespace Api
