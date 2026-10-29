/**
 * @file CalendarController.hpp
 * @brief The caller's Google Calendar (calendar/): connect through Google's
 *        consent (the callback is public and finds the user by a one-time
 *        state), disconnect, the events of a range, the free slots of a day,
 *        and adding, moving and removing an event (what Ani's confirmed
 *        proposals call; the person may call them too).
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class CalendarController : public HttpController<CalendarController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(CalendarController::status, "/api/v1/calendar", Get);
    ADD_METHOD_TO(CalendarController::connect, "/api/v1/calendar/connect", Post);
    ADD_METHOD_TO(CalendarController::oauthCallback, "/api/v1/calendar/oauth/callback", Get);
    ADD_METHOD_TO(CalendarController::disconnect, "/api/v1/calendar", Delete);
    ADD_METHOD_TO(CalendarController::events, "/api/v1/calendar/events", Get);
    ADD_METHOD_TO(CalendarController::add, "/api/v1/calendar/events", Post);
    ADD_METHOD_TO(CalendarController::move, "/api/v1/calendar/events/{id}", Patch);
    ADD_METHOD_TO(CalendarController::remove, "/api/v1/calendar/events/{id}", Delete);
    ADD_METHOD_TO(CalendarController::freeSlots, "/api/v1/calendar/free", Get);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void status(const HttpRequestPtr& req, Callback&& callback);
    void connect(const HttpRequestPtr& req, Callback&& callback);
    void oauthCallback(const HttpRequestPtr& req, Callback&& callback);
    void disconnect(const HttpRequestPtr& req, Callback&& callback);
    void events(const HttpRequestPtr& req, Callback&& callback);
    void add(const HttpRequestPtr& req, Callback&& callback);
    void move(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void remove(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void freeSlots(const HttpRequestPtr& req, Callback&& callback);
};

}  // namespace Api
