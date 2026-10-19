/**
 * @file PracticeController.hpp
 * @brief /api/v1/practice: the owner's freelance practice. Clients, deals with
 *        stages (lead, negotiation, active, done, lost), the incomes and
 *        expenses of the practice, and the months with totals per currency.
 *        A deal's next steps are tasks with its deal_id (TasksController).
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class PracticeController : public HttpController<PracticeController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(PracticeController::listClients, "/api/v1/practice/clients", Get);
    ADD_METHOD_TO(PracticeController::createClient, "/api/v1/practice/clients", Post);
    ADD_METHOD_TO(PracticeController::getClient, "/api/v1/practice/clients/{id}", Get);
    ADD_METHOD_TO(PracticeController::updateClient, "/api/v1/practice/clients/{id}", Patch);
    ADD_METHOD_TO(PracticeController::deleteClient, "/api/v1/practice/clients/{id}", Delete);
    ADD_METHOD_TO(PracticeController::listDeals, "/api/v1/practice/deals", Get);
    ADD_METHOD_TO(PracticeController::createDeal, "/api/v1/practice/deals", Post);
    ADD_METHOD_TO(PracticeController::getDeal, "/api/v1/practice/deals/{id}", Get);
    ADD_METHOD_TO(PracticeController::updateDeal, "/api/v1/practice/deals/{id}", Patch);
    ADD_METHOD_TO(PracticeController::deleteDeal, "/api/v1/practice/deals/{id}", Delete);
    ADD_METHOD_TO(PracticeController::listEntries, "/api/v1/practice/entries", Get);
    ADD_METHOD_TO(PracticeController::createEntry, "/api/v1/practice/entries", Post);
    ADD_METHOD_TO(PracticeController::getEntry, "/api/v1/practice/entries/{id}", Get);
    ADD_METHOD_TO(PracticeController::updateEntry, "/api/v1/practice/entries/{id}", Patch);
    ADD_METHOD_TO(PracticeController::deleteEntry, "/api/v1/practice/entries/{id}", Delete);
    ADD_METHOD_TO(PracticeController::listMonths, "/api/v1/practice/months", Get);
    ADD_METHOD_TO(PracticeController::getMonth, "/api/v1/practice/months/{month}", Get);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void listClients(const HttpRequestPtr& req, Callback&& callback);
    void createClient(const HttpRequestPtr& req, Callback&& callback);
    void getClient(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void updateClient(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteClient(const HttpRequestPtr& req, Callback&& callback, const std::string& id);

    void listDeals(const HttpRequestPtr& req, Callback&& callback);
    void createDeal(const HttpRequestPtr& req, Callback&& callback);
    void getDeal(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void updateDeal(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteDeal(const HttpRequestPtr& req, Callback&& callback, const std::string& id);

    void listEntries(const HttpRequestPtr& req, Callback&& callback);
    void createEntry(const HttpRequestPtr& req, Callback&& callback);
    void getEntry(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void updateEntry(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteEntry(const HttpRequestPtr& req, Callback&& callback, const std::string& id);

    void listMonths(const HttpRequestPtr& req, Callback&& callback);
    void getMonth(const HttpRequestPtr& req, Callback&& callback, const std::string& month);
};

}  // namespace Api
