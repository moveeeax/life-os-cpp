/**
 * @file MetricsController.hpp
 * @brief /api/v1/metrics: the catalog of daily series a number goal or a habit
 *        can read, expanded for the caller (their currencies, categories and
 *        habits ticked by hand) with whether each is available, and one series
 *        read for a range of days
 *        (docs/superpowers/specs/2026-10-20-metric-sources-design.md).
 */

#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class MetricsController : public HttpController<MetricsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(MetricsController::list, "/api/v1/metrics", Get);
    ADD_METHOD_TO(MetricsController::series, "/api/v1/metrics/series", Get);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void list(const HttpRequestPtr& req, Callback&& callback);
    void series(const HttpRequestPtr& req, Callback&& callback);
};

}  // namespace Api
