/**
 * @file Register.cpp
 * @brief Body for src/email/jobs/Register.hpp — compiled once into app_core.
 */

#include "email/jobs/Register.hpp"

#include <nlohmann/json.hpp>

#include "email/AccountEmailWorker.hpp"
#include "email/GenericEmail.hpp"
#include "jobs/Dispatcher.hpp"

namespace Email {

void register_jobs(Jobs::Dispatcher& d) {
    using nlohmann::json;
    // Account emails (confirm / reset / change-email / invite). Throws on
    // render/SMTP failure → retried, then DLQ'd.
    d.register_handler(AccountEmails::kJobType,
                       [](const json& payload) { return AccountEmails::process_job(payload); });
    // Generic ad-hoc email for any app code (not tied to account flows). Same
    // throw-on-failure → retry/DLQ contract.
    d.register_handler(SendEmail::kJobType, [](const json& payload) { return SendEmail::process_job(payload); });
}

}  // namespace Email
