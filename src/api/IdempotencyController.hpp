/**
 * @file IdempotencyController.hpp
 * @brief Which Idempotency-Keys of the caller the server has already applied:
 *        an offline queue asks before it lays its pending writes over a
 *        screen's answer, so a write in flight is not counted twice.
 *
 * Declarations only — the handler body lives in IdempotencyController.cpp
 * (compiled once into app_core; docs/ARCHITECTURE.md §4). The route
 * macros (ADD_METHOD_TO) must stay in this header: Drogon's METHOD_LIST
 * registration is part of the class definition, and
 * scripts/check-routes-registered.sh greps the src/api headers for them.
 */

#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class IdempotencyController : public HttpController<IdempotencyController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(IdempotencyController::seen, "/api/v1/idempotency/seen", Get);
    METHOD_LIST_END

    /// ?keys=k1,k2 (1..100, each 1..200 characters) → {data: [the seen keys, in
    /// the order asked]}; only the caller's own keys, kept ttl_sec (24 h).
    void seen(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
};

}  // namespace Api
