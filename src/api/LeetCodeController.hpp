/**
 * @file LeetCodeController.hpp
 * @brief The caller's LeetCode profile: the public username whose solved
 *        counts are pulled every hour (leetcode/Sync.hpp) into the
 *        metrics catalog (leetcode:solved and per difficulty), so a habit
 *        can take them as its source. Setting it queues a pull at once.
 */

#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace Api {

using namespace drogon;

class LeetCodeController : public HttpController<LeetCodeController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(LeetCodeController::profile, "/api/v1/leetcode/profile", Get);
    ADD_METHOD_TO(LeetCodeController::setProfile, "/api/v1/leetcode/profile", Put);
    ADD_METHOD_TO(LeetCodeController::removeProfile, "/api/v1/leetcode/profile", Delete);
    METHOD_LIST_END

    using Callback = std::function<void(const HttpResponsePtr&)>;

    void profile(const HttpRequestPtr& req, Callback&& callback);
    void setProfile(const HttpRequestPtr& req, Callback&& callback);
    void removeProfile(const HttpRequestPtr& req, Callback&& callback);
};

}  // namespace Api
