/**
 * @file AniSettingsController.cpp
 * @brief Body for src/api/AniSettingsController.hpp.
 */

#include "api/AniSettingsController.hpp"

#include <exception>
#include <string>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "repositories/agent/HookRepository.hpp"
#include "repositories/agent/InboxRepository.hpp"
#include "repositories/agent/PrefsRepository.hpp"
#include "storage/ObjectStore.hpp"
#include "utils/Config.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

namespace {

using json = nlohmann::json;

#define ANI_SETTINGS_GUARD(req, callback, owner) \
    API_REQUIRE_USER(req, callback, owner);      \
    API_REFUSE_API_KEY(req, callback)

/// The site's own address, as the account e-mails link to it.
std::string base_url() {
    std::string base = "http://localhost:8080";
    if (Config::is_initialized()) {
        base = Config::get().get<std::string>("app.base_url", "APP_BASE_URL", base);
    }
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base;
}

json hook_view(const std::string& owner, const std::string& token) {
    return json{{"url", base_url() + "/api/v1/ani/hook/" + token},
                {"recent", Repositories::Agent::InboxRepository().recent(owner, 10)}};
}

}  // namespace

void AniSettingsController::prefs(const HttpRequestPtr& req, Callback&& callback) {
    ANI_SETTINGS_GUARD(req, callback, owner);
    try {
        callback(Response::ok(json{{"data", Repositories::Agent::PrefsRepository().get(owner)}}));
    } catch (const std::exception& e) {
        spdlog::error("ani.prefs failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniSettingsController::savePrefs(const HttpRequestPtr& req, Callback&& callback) {
    ANI_SETTINGS_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto bare_it = body.find("bare");
    if (bare_it == body.end() || !bare_it->is_boolean()) {
        callback(ErrorResponse::bad_request("invalid_body", "bare must be true or false"));
        return;
    }
    const bool on = bare_it->get<bool>();
    const auto adult_it = body.find("adult");
    if (on && !(adult_it != body.end() && adult_it->is_boolean() && adult_it->get<bool>())) {
        callback(ErrorResponse::unprocessable("adult_required", "the bare outfit is only for adults"));
        return;
    }
    try {
        Repositories::Agent::PrefsRepository repo;
        repo.set_bare(owner, on);
        callback(Response::ok(json{{"data", repo.get(owner)}}));
    } catch (const std::exception& e) {
        spdlog::error("ani.prefs save failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniSettingsController::bare(const HttpRequestPtr& req, Callback&& callback) {
    ANI_SETTINGS_GUARD(req, callback, owner);
    try {
        if (!Repositories::Agent::PrefsRepository().get(owner).value("bare", false)) {
            callback(ErrorResponse::forbidden("bare_off", "the bare outfit is off in Ani's settings"));
            return;
        }
        Storage::ObjectStore* store = Storage::object_store();
        if (store == nullptr) {
            callback(ErrorResponse::service_unavailable("storage_unavailable", "file storage is not configured"));
            return;
        }
        const std::string model = store->private_key("ani", kBareFolder, "Ani.pmx");
        const std::string texture = store->private_key("ani", kBareFolder, "kanata_body.png");
        auto resp = Response::ok(json{{"data",
                                       {{"model_url", store->download_url(model, kLinkSeconds, "inline")},
                                        {"texture_url", store->download_url(texture, kLinkSeconds, "inline")},
                                        {"expires_in", kLinkSeconds}}}});
        // The links are bearer credentials for two minutes: no cache keeps them.
        resp->addHeader("Cache-Control", "no-store");
        callback(resp);
    } catch (const std::exception& e) {
        spdlog::error("ani.bare failed: {}", e.what());
        callback(ErrorResponse::service_unavailable("storage_unavailable", "file storage did not answer"));
    }
}

void AniSettingsController::webhook(const HttpRequestPtr& req, Callback&& callback) {
    ANI_SETTINGS_GUARD(req, callback, owner);
    try {
        auto resp = Response::ok(json{{"data", hook_view(owner, Repositories::Agent::HookRepository().ensure(owner))}});
        resp->addHeader("Cache-Control", "no-store");
        callback(resp);
    } catch (const std::exception& e) {
        spdlog::error("ani.webhook failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniSettingsController::rotateWebhook(const HttpRequestPtr& req, Callback&& callback) {
    ANI_SETTINGS_GUARD(req, callback, owner);
    try {
        auto resp = Response::ok(json{{"data", hook_view(owner, Repositories::Agent::HookRepository().rotate(owner))}});
        resp->addHeader("Cache-Control", "no-store");
        callback(resp);
    } catch (const std::exception& e) {
        spdlog::error("ani.webhook rotate failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

}  // namespace Api
