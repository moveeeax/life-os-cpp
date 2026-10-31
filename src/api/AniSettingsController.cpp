/**
 * @file AniSettingsController.cpp
 * @brief Body for src/api/AniSettingsController.hpp.
 */

#include "api/AniSettingsController.hpp"

#include <exception>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "agent/Reminders.hpp"
#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/PathNormalize.hpp"
#include "repositories/Zones.hpp"
#include "repositories/agent/DealRepository.hpp"
#include "repositories/agent/FactRepository.hpp"
#include "repositories/agent/HookRepository.hpp"
#include "repositories/agent/InboxRepository.hpp"
#include "repositories/agent/PrefsRepository.hpp"
#include "repositories/agent/ReminderRepository.hpp"
#include "security/Audit.hpp"
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
                {"recent", Repositories::Agent::InboxRepository().recent(owner, 20)}};
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
    const auto muted_it = body.find("muted");
    if ((bare_it == body.end() && muted_it == body.end()) || (bare_it != body.end() && !bare_it->is_boolean())) {
        callback(ErrorResponse::bad_request("invalid_body", "bare must be true or false, or muted a list"));
        return;
    }
    // The kinds of her own lines the page can mute ("Not about this" on Now).
    static const std::set<std::string> kKinds = {
        "brief", "evening", "break", "stretch", "hello", "telegram", "webhook", "reminder", "calendar"};
    std::vector<std::string> muted;
    if (muted_it != body.end()) {
        if (!muted_it->is_array() || muted_it->size() > kKinds.size()) {
            callback(ErrorResponse::bad_request("invalid_muted", "muted is a list of line kinds"));
            return;
        }
        for (const auto& k : *muted_it) {
            if (!k.is_string() || kKinds.count(k.get<std::string>()) == 0) {
                callback(ErrorResponse::bad_request("invalid_muted", "muted is a list of line kinds"));
                return;
            }
            muted.push_back(k.get<std::string>());
        }
    }
    if (bare_it != body.end() && bare_it->get<bool>()) {
        const auto adult_it = body.find("adult");
        if (!(adult_it != body.end() && adult_it->is_boolean() && adult_it->get<bool>())) {
            callback(ErrorResponse::unprocessable("adult_required", "the bare outfit is only for adults"));
            return;
        }
    }
    try {
        Repositories::Agent::PrefsRepository repo;
        if (bare_it != body.end()) {
            repo.set_bare(owner, bare_it->get<bool>());
        }
        if (muted_it != body.end()) {
            repo.set_muted(owner, muted);
        }
        callback(Response::ok(json{{"data", repo.get(owner)}}));
    } catch (const std::exception& e) {
        spdlog::error("ani.prefs save failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

namespace {

std::optional<std::string> fact_text(const json& body, const AniSettingsController::Callback& callback) {
    const std::string text =
        body.contains("text") && body["text"].is_string() ? body["text"].get<std::string>() : std::string();
    std::size_t n = 0;
    for (const unsigned char c : text) {
        n += (c & 0xC0) != 0x80 ? 1 : 0;
    }
    if (n < 1 || n > 200) {
        callback(ErrorResponse::bad_request("invalid_text", "a fact is 1 to 200 characters"));
        return std::nullopt;
    }
    return text;
}

}  // namespace

void AniSettingsController::facts(const HttpRequestPtr& req, Callback&& callback) {
    ANI_SETTINGS_GUARD(req, callback, owner);
    try {
        callback(Response::ok(json{{"data", Repositories::Agent::FactRepository().list(owner)}}));
    } catch (const std::exception& e) {
        spdlog::error("ani.facts failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniSettingsController::addFact(const HttpRequestPtr& req, Callback&& callback) {
    ANI_SETTINGS_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto text = fact_text(body, callback);
    if (!text) {
        return;
    }
    try {
        const auto id = Repositories::Agent::FactRepository().add(owner, *text, "page");
        if (!id) {
            callback(ErrorResponse::conflict("too_many_facts", "50 facts at most"));
            return;
        }
        callback(Response::created(json{{"data", {{"id", *id}}}}));
    } catch (const std::exception& e) {
        spdlog::error("ani.facts add failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniSettingsController::editFact(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    ANI_SETTINGS_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto text = fact_text(body, callback);
    if (!text) {
        return;
    }
    try {
        if (!is_valid_uuid(id) || !Repositories::Agent::FactRepository().update(owner, id, *text)) {
            callback(ErrorResponse::not_found("fact_not_found"));
            return;
        }
        no_content(callback);
    } catch (const std::exception& e) {
        spdlog::error("ani.facts edit failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniSettingsController::removeFact(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    ANI_SETTINGS_GUARD(req, callback, owner);
    try {
        if (!is_valid_uuid(id) || !Repositories::Agent::FactRepository().remove(owner, id)) {
            callback(ErrorResponse::not_found("fact_not_found"));
            return;
        }
        no_content(callback);
    } catch (const std::exception& e) {
        spdlog::error("ani.facts remove failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniSettingsController::deals(const HttpRequestPtr& req, Callback&& callback) {
    ANI_SETTINGS_GUARD(req, callback, owner);
    const std::string tz = req->getParameter("tz");
    try {
        callback(Response::ok(json{{"data",
                                    Repositories::Agent::DealRepository().week(
                                        owner, Repositories::Zones::is_zone(tz) ? tz : std::string("UTC"))}}));
    } catch (const std::exception& e) {
        spdlog::error("ani.deals failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniSettingsController::removeDeal(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    ANI_SETTINGS_GUARD(req, callback, owner);
    try {
        if (!is_valid_uuid(id) || !Repositories::Agent::DealRepository().remove(owner, id)) {
            callback(ErrorResponse::not_found("deal_not_found"));
            return;
        }
        no_content(callback);
    } catch (const std::exception& e) {
        spdlog::error("ani.deals remove failed: {}", e.what());
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

void AniSettingsController::reminders(const HttpRequestPtr& req, Callback&& callback) {
    ANI_SETTINGS_GUARD(req, callback, owner);
    try {
        callback(Response::ok(json{{"data", Repositories::Agent::ReminderRepository().list(owner)}}));
    } catch (const std::exception& e) {
        spdlog::error("ani.reminders failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniSettingsController::addReminder(const HttpRequestPtr& req, Callback&& callback) {
    ANI_SETTINGS_GUARD(req, callback, owner);
    const auto body = json::parse(req->body(), nullptr, false);
    auto in = ::Agent::Reminders::parse(body.is_discarded() ? json() : body, "UTC");
    if (const auto* err = std::get_if<std::string>(&in)) {
        callback(ErrorResponse::bad_request("invalid_reminder", *err));
        return;
    }
    const auto& r = std::get<::Agent::Reminders::Input>(in);
    try {
        const auto made = Repositories::Agent::ReminderRepository().add(owner, r.text, r.at, r.days, r.tz);
        if (!made) {
            callback(ErrorResponse::conflict("too_many", "at most 20 reminders"));
            return;
        }
        Security::Audit::record(owner, "ani.reminder.add", "ani_reminder", (*made)["id"], json{{"via", "api"}});
        auto resp = Response::ok(json{{"data", *made}});
        resp->setStatusCode(k201Created);
        callback(resp);
    } catch (const std::exception& e) {
        spdlog::error("ani.reminders add failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniSettingsController::removeReminder(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    ANI_SETTINGS_GUARD(req, callback, owner);
    try {
        if (!Repositories::Agent::ReminderRepository().remove(owner, id)) {
            callback(ErrorResponse::not_found("not_found"));
            return;
        }
        Security::Audit::record(owner, "ani.reminder.remove", "ani_reminder", id, json{{"via", "api"}});
        no_content(callback);
    } catch (const std::exception& e) {
        spdlog::error("ani.reminders remove failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

}  // namespace Api
