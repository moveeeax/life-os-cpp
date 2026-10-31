/**
 * @file NowDayController.hpp
 * @brief The day ribbon on Now in one call (the calendar's events and free
 *        slots, focus sessions, workouts, tasks with a time), and the record of
 *        a focus session when the page's timer ends one.
 */

#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class NowDayController : public HttpController<NowDayController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(NowDayController::day, "/api/v1/now/day", Get);
    ADD_METHOD_TO(NowDayController::addFocus, "/api/v1/now/focus-sessions", Post);
    ADD_METHOD_TO(NowDayController::focusStats, "/api/v1/now/focus-stats", Get);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void day(const HttpRequestPtr& req, Callback&& callback);
    void addFocus(const HttpRequestPtr& req, Callback&& callback);
    /// Focus by the person's clock: this week, the last seven days, the hours of the day.
    void focusStats(const HttpRequestPtr& req, Callback&& callback);
};

}  // namespace Api
