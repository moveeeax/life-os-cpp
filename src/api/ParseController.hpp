/**
 * @file ParseController.hpp
 * @brief The /api/v1/parse routes: one phrase through one model call, the
 *        rows of every kind it holds (food, money, tasks, water). The rows are
 *        accepted in their modules as from the module's own parse, or applied
 *        by the worker when the request asks for it. Rows belong to the caller.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class ParseController : public HttpController<ParseController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ParseController::start, "/api/v1/parse", Post);
    ADD_METHOD_TO(ParseController::status, "/api/v1/parse/{id}", Get);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void start(const HttpRequestPtr& req, Callback&& callback);
    void status(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
};

}  // namespace Api
