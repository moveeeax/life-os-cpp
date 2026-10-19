/**
 * @file MedicalController.hpp
 * @brief The family medical archive: people, doctors and clinics, visits,
 *        records (their files are FilesController's) and medications. A
 *        visit's follow-up date and a medication's end date can open a task.
 *        Owner-scoped like every module; 404 while medical is off. It keeps
 *        an archive and its dates, it never suggests a diagnosis or a dose.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class MedicalController : public HttpController<MedicalController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(MedicalController::listPeople, "/api/v1/medical/people", Get);
    ADD_METHOD_TO(MedicalController::createPerson, "/api/v1/medical/people", Post);
    ADD_METHOD_TO(MedicalController::updatePerson, "/api/v1/medical/people/{id}", Patch);
    ADD_METHOD_TO(MedicalController::deletePerson, "/api/v1/medical/people/{id}", Delete);
    ADD_METHOD_TO(MedicalController::listProviders, "/api/v1/medical/providers", Get);
    ADD_METHOD_TO(MedicalController::createProvider, "/api/v1/medical/providers", Post);
    ADD_METHOD_TO(MedicalController::updateProvider, "/api/v1/medical/providers/{id}", Patch);
    ADD_METHOD_TO(MedicalController::deleteProvider, "/api/v1/medical/providers/{id}", Delete);
    ADD_METHOD_TO(MedicalController::listVisits, "/api/v1/medical/visits", Get);
    ADD_METHOD_TO(MedicalController::createVisit, "/api/v1/medical/visits", Post);
    ADD_METHOD_TO(MedicalController::updateVisit, "/api/v1/medical/visits/{id}", Patch);
    ADD_METHOD_TO(MedicalController::deleteVisit, "/api/v1/medical/visits/{id}", Delete);
    ADD_METHOD_TO(MedicalController::visitTask, "/api/v1/medical/visits/{id}/task", Post);
    ADD_METHOD_TO(MedicalController::listRecords, "/api/v1/medical/records", Get);
    ADD_METHOD_TO(MedicalController::createRecord, "/api/v1/medical/records", Post);
    ADD_METHOD_TO(MedicalController::updateRecord, "/api/v1/medical/records/{id}", Patch);
    ADD_METHOD_TO(MedicalController::deleteRecord, "/api/v1/medical/records/{id}", Delete);
    ADD_METHOD_TO(MedicalController::listMedications, "/api/v1/medical/medications", Get);
    ADD_METHOD_TO(MedicalController::createMedication, "/api/v1/medical/medications", Post);
    ADD_METHOD_TO(MedicalController::updateMedication, "/api/v1/medical/medications/{id}", Patch);
    ADD_METHOD_TO(MedicalController::deleteMedication, "/api/v1/medical/medications/{id}", Delete);
    ADD_METHOD_TO(MedicalController::medicationTask, "/api/v1/medical/medications/{id}/task", Post);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void listPeople(const HttpRequestPtr& req, Callback&& callback);
    void createPerson(const HttpRequestPtr& req, Callback&& callback);
    void updatePerson(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deletePerson(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void listProviders(const HttpRequestPtr& req, Callback&& callback);
    void createProvider(const HttpRequestPtr& req, Callback&& callback);
    void updateProvider(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteProvider(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void listVisits(const HttpRequestPtr& req, Callback&& callback);
    void createVisit(const HttpRequestPtr& req, Callback&& callback);
    void updateVisit(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteVisit(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void visitTask(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void listRecords(const HttpRequestPtr& req, Callback&& callback);
    void createRecord(const HttpRequestPtr& req, Callback&& callback);
    void updateRecord(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteRecord(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void listMedications(const HttpRequestPtr& req, Callback&& callback);
    void createMedication(const HttpRequestPtr& req, Callback&& callback);
    void updateMedication(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteMedication(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void medicationTask(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
};

}  // namespace Api
