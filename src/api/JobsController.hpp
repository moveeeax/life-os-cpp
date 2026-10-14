/**
 * @file JobsController.hpp
 * @brief Jobs queue HTTP controller
 * @details Handles /api/v1/jobs endpoints for submitting and querying background jobs
 *
 * Declarations only — the handler bodies live in JobsController.cpp
 * (compiled once into app_core; docs/ARCHITECTURE.md §4). The route
 * macros (ADD_METHOD_TO) must stay in this header: Drogon's METHOD_LIST
 * registration is part of the class definition, and
 * scripts/check-routes-registered.sh greps the src/api headers for them.
 */

#pragma once

#include <functional>
#include <string>

#include <drogon/HttpController.h>

#include <nlohmann/json_fwd.hpp>

namespace Api {

using namespace drogon;
using json = nlohmann::json;

// The jobs API is an ops surface (queue state, payloads, requeue/cancel) —
// admin-only when auth is on (API_REQUIRE_ADMIN no-ops under AUTH_MODE=none).

/**
 * @brief Jobs controller — submit, query, and cancel background jobs
 */
class JobsController : public HttpController<JobsController> {
public:
    METHOD_LIST_BEGIN
    // Fixed-path routes must be registered before parameterized routes so
    // that /api/v1/jobs/dlq does not accidentally match /api/v1/jobs/{1}.
    ADD_METHOD_TO(JobsController::listDlq, "/api/v1/jobs/dlq", Get);
    ADD_METHOD_TO(JobsController::requeueDlq, "/api/v1/jobs/dlq/{1}/requeue", Post);
    ADD_METHOD_TO(JobsController::listJobs, "/api/v1/jobs", Get);
    ADD_METHOD_TO(JobsController::submitJob, "/api/v1/jobs", Post);
    ADD_METHOD_TO(JobsController::getJobStatus, "/api/v1/jobs/{1}", Get);
    ADD_METHOD_TO(JobsController::cancelJob, "/api/v1/jobs/{1}", Delete);
    METHOD_LIST_END

    void listJobs(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);

    void submitJob(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);

    /**
     * @brief Job types an admin may enqueue by hand through POST /jobs: the
     *        demo handlers (echo, slow, fail) and the rates refresh
     *        (money_rates). Everything else is enqueued by the app with a
     *        payload it built itself: webhook.deliver would turn the endpoint
     *        into an SSRF probe, email.send / account_email into a mail relay,
     *        and the parse / sync jobs need rows the app writes first.
     *        Anything else answers 400 job_type_not_allowed.
     */
    static bool manual_type_allowed(const std::string& type);

    void getJobStatus(const HttpRequestPtr& req,
                      std::function<void(const HttpResponsePtr&)>&& callback,
                      const std::string& id);

    void listDlq(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);

    void requeueDlq(const HttpRequestPtr& req,
                    std::function<void(const HttpResponsePtr&)>&& callback,
                    const std::string& id);

    void cancelJob(const HttpRequestPtr& req,
                   std::function<void(const HttpResponsePtr&)>&& callback,
                   const std::string& id);
};

}  // namespace Api
