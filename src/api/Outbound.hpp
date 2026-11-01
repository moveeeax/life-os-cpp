/**
 * @file Outbound.hpp
 * @brief Handlers that wait on an outside service (Google Calendar, the
 *        Telegram gateway) do that part on Utils::BlockingPool::outbound(),
 *        not on Drogon's IO threads: one slow upstream used to hold one of
 *        the API's few loops for up to 30 s, and every connection on that
 *        loop waited with it. The task answers the request itself (Drogon
 *        takes a callback from any thread).
 */

#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <utility>

#include <drogon/HttpResponse.h>
#include <spdlog/spdlog.h>

#include "utils/BlockingPool.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api::Outbound {

using Callback = std::function<void(const drogon::HttpResponsePtr&)>;

/// Runs @p work with the request's @p callback on the outbound pool. A full
/// pool answers 503 busy at once; an exception out of @p work answers 500
/// (logged with @p what).
inline void run(Callback&& callback, const char* what, std::function<void(const Callback&)> work) {
    auto respond = std::make_shared<Callback>(std::move(callback));
    const bool queued = Utils::BlockingPool::outbound().try_submit([respond, what, work = std::move(work)] {
        try {
            work(*respond);
        } catch (const std::exception& e) {
            spdlog::error("{} failed: {}", what, e.what());
            (*respond)(ErrorResponse::internal_error());
        }
    });
    if (!queued) {
        (*respond)(ErrorResponse::service_unavailable("busy", "the server is busy, try again"));
    }
}

}  // namespace Api::Outbound
