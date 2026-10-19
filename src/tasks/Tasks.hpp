/**
 * @file Tasks.hpp
 * @brief Recurring background tasks scheduled on Drogon's event loop.
 *
 * Core schedules the metric refreshers, the outbox drain and the fitness,
 * money rates and money advisor ticks here. Kept as a thin facade so callers
 * don't reach into Drogon directly and so shutdown() can drain timers
 * cleanly on Core::shutdown().
 */

#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

namespace Tasks {

using TaskCallback = std::function<void()>;

namespace detail {

inline std::map<std::string, trantor::TimerId>& timers() {
    static std::map<std::string, trantor::TimerId> m;
    return m;
}

inline std::mutex& mu() {
    static std::mutex m;
    return m;
}

inline bool& initialized_flag() {
    static bool b = false;
    return b;
}

// One-shot startup runs (schedule_recurring's first_run), keyed like timers().
inline std::map<std::string, trantor::TimerId>& first_runs() {
    static std::map<std::string, trantor::TimerId> m;
    return m;
}

// The loop the timers run on: Drogon's main loop, or the one a test set.
inline trantor::EventLoop*& loop_override() {
    static trantor::EventLoop* loop = nullptr;
    return loop;
}

inline trantor::EventLoop* loop() {
    return loop_override() != nullptr ? loop_override() : drogon::app().getLoop();
}

inline TaskCallback guarded(const std::string& task_id, TaskCallback callback) {
    return [task_id, cb = std::move(callback)]() {
        try {
            cb();
        } catch (const std::exception& e) {
            spdlog::error("Recurring task '{}' failed: {}", task_id, e.what());
        }
    };
}

}  // namespace detail

inline void initialize() {
    if (detail::initialized_flag())
        throw std::runtime_error("Tasks already initialized");
    detail::initialized_flag() = true;
    spdlog::info("Task scheduler initialized");
}

inline bool is_initialized() {
    return detail::initialized_flag();
}

/**
 * @brief Run @p callback every @p interval. The loop's runEvery fires first
 *        one interval after scheduling, so a task whose interval is longer
 *        than the time between deploys would never run: pass @p first_run to
 *        also run it once that long after scheduling (the loop must be
 *        running by then; a delay of a few seconds covers startup).
 * @return false when @p task_id is already scheduled.
 */
inline bool schedule_recurring(const std::string& task_id,
                               std::chrono::milliseconds interval,
                               TaskCallback callback,
                               std::optional<std::chrono::milliseconds> first_run = std::nullopt) {
    if (!detail::initialized_flag())
        throw std::runtime_error("Tasks not initialized");
    std::lock_guard<std::mutex> lock(detail::mu());
    if (detail::timers().count(task_id) > 0) {
        spdlog::warn("Task '{}' already scheduled", task_id);
        return false;
    }
    auto run = detail::guarded(task_id, std::move(callback));
    if (first_run) {
        detail::first_runs()[task_id] = detail::loop()->runAfter(first_run->count() / 1000.0, run);
    }
    detail::timers()[task_id] = detail::loop()->runEvery(interval.count() / 1000.0, std::move(run));
    return true;
}

inline bool cancel(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(detail::mu());
    auto it = detail::timers().find(task_id);
    if (it == detail::timers().end())
        return false;
    detail::loop()->invalidateTimer(it->second);
    detail::timers().erase(it);
    if (auto first = detail::first_runs().find(task_id); first != detail::first_runs().end()) {
        detail::loop()->invalidateTimer(first->second);
        detail::first_runs().erase(first);
    }
    return true;
}

inline void shutdown() {
    if (!detail::initialized_flag())
        return;
    std::lock_guard<std::mutex> lock(detail::mu());
    for (auto& [id, tid] : detail::timers()) {
        detail::loop()->invalidateTimer(tid);
    }
    for (auto& [id, tid] : detail::first_runs()) {
        detail::loop()->invalidateTimer(tid);
    }
    detail::timers().clear();
    detail::first_runs().clear();
    detail::initialized_flag() = false;
    spdlog::info("Task scheduler shut down");
}

}  // namespace Tasks
