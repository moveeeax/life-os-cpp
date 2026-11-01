/**
 * @file LeetCodeController.cpp
 * @brief Body for src/api/LeetCodeController.hpp.
 */

#include "api/LeetCodeController.hpp"

#include <chrono>
#include <exception>
#include <string>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "cache/Cache.hpp"
#include "jobs/Jobs.hpp"
#include "leetcode/Sync.hpp"
#include "repositories/Zones.hpp"
#include "repositories/leetcode/ProfileRepository.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Repositories::LeetCode::ProfileRepository;

}  // namespace

void LeetCodeController::profile(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    try {
        const auto p = ProfileRepository().get(owner);
        callback(Response::ok(json{{"data", p ? *p : json(nullptr)}}));
    } catch (const std::exception& e) {
        spdlog::error("leetcode.profile failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void LeetCodeController::setProfile(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto u = body.find("username");
    if (u == body.end() || !u->is_string() || !::LeetCode::valid_username(u->get<std::string>())) {
        callback(ErrorResponse::bad_request("invalid_username", "username is 1 to 30 letters, digits, _ . -"));
        return;
    }
    const auto t = body.find("tz");
    const std::string tz = t != body.end() && t->is_string() ? t->get<std::string>() : "UTC";
    if (!Repositories::Zones::is_zone(tz)) {
        callback(ErrorResponse::bad_request("invalid_tz", "tz must be an IANA zone like Asia/Almaty"));
        return;
    }
    try {
        ProfileRepository repo;
        repo.set(owner, u->get<std::string>(), tz);
        // Only this user's profile is pulled, at most once a minute however often it is saved: a pull
        // of everyone per save could have LeetCode throttle the worker for every user.
        if (Jobs::is_initialized() &&
            (!Cache::is_initialized() ||
             Cache::get().set_nx("leetcode:sync:queued:" + owner, "1", std::chrono::seconds(60)))) {
            Jobs::get().submit(::LeetCode::kSyncJob, json{{"owner", owner}});
        }
        callback(Response::ok(json{{"data", *repo.get(owner)}}));
    } catch (const std::exception& e) {
        spdlog::error("leetcode.profile set failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void LeetCodeController::removeProfile(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    try {
        if (!ProfileRepository().remove(owner)) {
            callback(ErrorResponse::not_found("leetcode_profile_not_found"));
            return;
        }
        no_content(callback);
    } catch (const std::exception& e) {
        spdlog::error("leetcode.profile remove failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

}  // namespace Api
