/**
 * @file TodayController.hpp
 * @brief The /api/v1/today routes: the day's digest of the Now screen and the
 *        focus of the day. Every handler checks the now switch, then the
 *        caller's user id; the focus belongs to that user only.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class TodayController : public HttpController<TodayController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(TodayController::today, "/api/v1/today", Get);
    ADD_METHOD_TO(TodayController::putFocus, "/api/v1/today/focus", Put);
    ADD_METHOD_TO(TodayController::deleteFocus, "/api/v1/today/focus", Delete);
    ADD_METHOD_TO(TodayController::focusDone, "/api/v1/today/focus/done", Post);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void today(const HttpRequestPtr& req, Callback&& callback);
    void putFocus(const HttpRequestPtr& req, Callback&& callback);
    void deleteFocus(const HttpRequestPtr& req, Callback&& callback);
    void focusDone(const HttpRequestPtr& req, Callback&& callback);
};

}  // namespace Api
