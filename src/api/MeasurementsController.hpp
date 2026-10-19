/**
 * @file MeasurementsController.hpp
 * @brief /api/v1/fitness/measurements: girths measured by hand (waist, chest,
 *        hips, neck, arm, thigh, in cm) for the Body section.
 *
 * The routes sit under the fitness module because Body is its section: the
 * module switch (404 `fitness`), then a user account (403 no_user_account).
 * No permission bit: the rows are the caller's own (owner_id), entered by
 * hand like the medical archive, not data of the Mi account, so they need
 * neither fitness:read nor a linked band.
 *
 * Declarations only; the bodies live in MeasurementsController.cpp. The route
 * macros stay here for scripts/check-routes-registered.sh.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class MeasurementsController : public HttpController<MeasurementsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(MeasurementsController::list, "/api/v1/fitness/measurements", Get);
    ADD_METHOD_TO(MeasurementsController::create, "/api/v1/fitness/measurements", Post);
    ADD_METHOD_TO(MeasurementsController::update, "/api/v1/fitness/measurements/{id}", Patch);
    ADD_METHOD_TO(MeasurementsController::remove, "/api/v1/fitness/measurements/{id}", Delete);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void list(const HttpRequestPtr& req, Callback&& callback);
    void create(const HttpRequestPtr& req, Callback&& callback);
    void update(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
    void remove(const HttpRequestPtr& req, Callback&& callback, const std::string& id);
};

}  // namespace Api
