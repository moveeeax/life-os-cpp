/**
 * @file AniLevelController.hpp
 * @brief GET /api/v1/ani/level: Ani's level, counted from the person's own
 *        records by the rules of domain/AniLevel.hpp (tasks, body, food, money:
 *        four scales, Ani's level is the lowest of them).
 *
 * Read only and computed on each read from the days since the count started
 * (migration 086). Open to API keys like the data it is counted from.
 *
 * Declarations only; the body lives in AniLevelController.cpp. The route macro
 * stays here for scripts/check-routes-registered.sh.
 */

#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class AniLevelController : public HttpController<AniLevelController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AniLevelController::get, "/api/v1/ani/level", Get);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void get(const HttpRequestPtr& req, Callback&& callback);
};

}  // namespace Api
