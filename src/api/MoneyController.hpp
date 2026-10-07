/**
 * @file MoneyController.hpp
 * @brief The /api/v1/money routes: currencies, accounts, categories, the ledger,
 *        transfers, the inbox, merchant memory, rates, reports and settings
 *        of the money module. Every handler checks the module switch, then
 *        the caller's user id; rows belong to that user only. The seven
 *        invariants of the model answer as 400 `invariant`.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class MoneyController : public HttpController<MoneyController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(MoneyController::listCurrencies, "/api/v1/money/currencies", Get);
    ADD_METHOD_TO(MoneyController::upsertCurrency, "/api/v1/money/currencies", Post);
    ADD_METHOD_TO(MoneyController::patchCurrency, "/api/v1/money/currencies/{code}", Patch);
    ADD_METHOD_TO(MoneyController::listAccounts, "/api/v1/money/accounts", Get);
    ADD_METHOD_TO(MoneyController::createAccount, "/api/v1/money/accounts", Post);
    ADD_METHOD_TO(MoneyController::getAccount, "/api/v1/money/accounts/{id}", Get);
    ADD_METHOD_TO(MoneyController::updateAccount, "/api/v1/money/accounts/{id}", Patch);
    ADD_METHOD_TO(MoneyController::deleteAccount, "/api/v1/money/accounts/{id}", Delete);
    ADD_METHOD_TO(MoneyController::listCategories, "/api/v1/money/categories", Get);
    ADD_METHOD_TO(MoneyController::createCategory, "/api/v1/money/categories", Post);
    ADD_METHOD_TO(MoneyController::getCategory, "/api/v1/money/categories/{id}", Get);
    ADD_METHOD_TO(MoneyController::updateCategory, "/api/v1/money/categories/{id}", Patch);
    ADD_METHOD_TO(MoneyController::deleteCategory, "/api/v1/money/categories/{id}", Delete);
    ADD_METHOD_TO(MoneyController::listTransactions, "/api/v1/money/transactions", Get);
    ADD_METHOD_TO(MoneyController::createTransaction, "/api/v1/money/transactions", Post);
    ADD_METHOD_TO(MoneyController::createTransactions, "/api/v1/money/transactions/batch", Post);
    ADD_METHOD_TO(MoneyController::getTransaction, "/api/v1/money/transactions/{id}", Get);
    ADD_METHOD_TO(MoneyController::updateTransaction, "/api/v1/money/transactions/{id}", Patch);
    ADD_METHOD_TO(MoneyController::deleteTransaction, "/api/v1/money/transactions/{id}", Delete);
    ADD_METHOD_TO(MoneyController::confirmTransaction, "/api/v1/money/transactions/{id}/confirm", Post);
    ADD_METHOD_TO(MoneyController::inbox, "/api/v1/money/inbox", Get);
    ADD_METHOD_TO(MoneyController::listTransfers, "/api/v1/money/transfers", Get);
    ADD_METHOD_TO(MoneyController::createTransfer, "/api/v1/money/transfers", Post);
    ADD_METHOD_TO(MoneyController::getTransfer, "/api/v1/money/transfers/{id}", Get);
    ADD_METHOD_TO(MoneyController::updateTransfer, "/api/v1/money/transfers/{id}", Patch);
    ADD_METHOD_TO(MoneyController::deleteTransfer, "/api/v1/money/transfers/{id}", Delete);
    ADD_METHOD_TO(MoneyController::merchants, "/api/v1/money/merchants", Get);
    ADD_METHOD_TO(MoneyController::patchMerchant, "/api/v1/money/merchants/{key}", Patch);
    ADD_METHOD_TO(MoneyController::rate, "/api/v1/money/rates", Get);
    ADD_METHOD_TO(MoneyController::convert, "/api/v1/money/rates/convert", Get);
    ADD_METHOD_TO(MoneyController::refreshRates, "/api/v1/money/rates/refresh", Post);
    ADD_METHOD_TO(MoneyController::periodReport, "/api/v1/money/reports/period", Get);
    ADD_METHOD_TO(MoneyController::balances, "/api/v1/money/reports/balances", Get);
    ADD_METHOD_TO(MoneyController::parseText, "/api/v1/money/parse", Post);
    ADD_METHOD_TO(MoneyController::parseReceipt, "/api/v1/money/parse/receipt", Post);
    ADD_METHOD_TO(MoneyController::parseStatus, "/api/v1/money/parse/{id}", Get);
    ADD_METHOD_TO(MoneyController::parseAccept, "/api/v1/money/parse/{id}/accept", Post);
    ADD_METHOD_TO(MoneyController::advisorReports, "/api/v1/money/advisor/reports", Get);
    ADD_METHOD_TO(MoneyController::advisorReport, "/api/v1/money/advisor/reports/{id}", Get);
    ADD_METHOD_TO(MoneyController::advisorRun, "/api/v1/money/advisor/run", Post);
    ADD_METHOD_TO(MoneyController::getSettings, "/api/v1/money/settings", Get);
    ADD_METHOD_TO(MoneyController::putSettings, "/api/v1/money/settings", Put);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void listCurrencies(const HttpRequestPtr& req, Callback&& callback);
    void upsertCurrency(const HttpRequestPtr& req, Callback&& callback);
    void patchCurrency(const HttpRequestPtr& req, Callback&& callback, const std::string& code);
    void listAccounts(const HttpRequestPtr& req, Callback&& callback);
    void createAccount(const HttpRequestPtr& req, Callback&& callback);
    void getAccount(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void updateAccount(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteAccount(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void listCategories(const HttpRequestPtr& req, Callback&& callback);
    void createCategory(const HttpRequestPtr& req, Callback&& callback);
    void getCategory(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void updateCategory(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteCategory(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void listTransactions(const HttpRequestPtr& req, Callback&& callback);
    void createTransaction(const HttpRequestPtr& req, Callback&& callback);
    void createTransactions(const HttpRequestPtr& req, Callback&& callback);
    void getTransaction(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void updateTransaction(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteTransaction(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void confirmTransaction(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void inbox(const HttpRequestPtr& req, Callback&& callback);
    void listTransfers(const HttpRequestPtr& req, Callback&& callback);
    void createTransfer(const HttpRequestPtr& req, Callback&& callback);
    void getTransfer(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void updateTransfer(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void deleteTransfer(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void merchants(const HttpRequestPtr& req, Callback&& callback);
    void patchMerchant(const HttpRequestPtr& req, Callback&& callback, const std::string& key);
    void rate(const HttpRequestPtr& req, Callback&& callback);
    void convert(const HttpRequestPtr& req, Callback&& callback);
    void refreshRates(const HttpRequestPtr& req, Callback&& callback);
    void periodReport(const HttpRequestPtr& req, Callback&& callback);
    void balances(const HttpRequestPtr& req, Callback&& callback);
    void parseText(const HttpRequestPtr& req, Callback&& callback);
    void parseReceipt(const HttpRequestPtr& req, Callback&& callback);
    void parseStatus(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void parseAccept(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void advisorReports(const HttpRequestPtr& req, Callback&& callback);
    void advisorReport(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void advisorRun(const HttpRequestPtr& req, Callback&& callback);
    void getSettings(const HttpRequestPtr& req, Callback&& callback);
    void putSettings(const HttpRequestPtr& req, Callback&& callback);

private:
    static bool require_enabled(const Callback& callback);
};

}  // namespace Api
