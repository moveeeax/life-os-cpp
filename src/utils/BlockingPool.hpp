/**
 * @file BlockingPool.hpp
 * @brief A small bounded thread pool for blocking work that must not run on
 *        Drogon's IO threads.
 *
 * A handler that has to wait on a slow upstream (the Xiaomi sign-in, a region
 * probe) hands that part to the pool and returns; the task calls the
 * response callback itself, and Drogon accepts a callback from any thread.
 * The pool has a fixed number of threads and a fixed number of waiting
 * tasks: when both are taken, try_submit() refuses at once and the handler
 * answers "busy" instead of piling requests up behind a stalled upstream.
 * Threads start with the first task, so a process that never submits
 * anything never starts them.
 */

#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Utils {

class BlockingPool {
public:
    using Task = std::function<void()>;

    /// @param threads worker threads (at least 1); @param max_queued tasks
    ///        that may wait for a free thread while every thread is busy.
    BlockingPool(std::size_t threads, std::size_t max_queued);
    ~BlockingPool();

    BlockingPool(const BlockingPool&) = delete;
    BlockingPool& operator=(const BlockingPool&) = delete;

    /// Queue @p task. False, and the task is not run, when threads + max_queued
    /// tasks are already running or waiting. A task that throws is caught and
    /// logged; it answers its own request before anything can throw.
    bool try_submit(Task task);

    /// Tasks running now plus tasks waiting for a thread.
    std::size_t in_flight() const;

    /// The pool the API's Xiaomi calls share: 4 threads, 16 waiting tasks.
    /// Never destroyed: a task still blocked in curl at process exit must not
    /// find its pool gone.
    static BlockingPool& shared();

    /// The pool of the API's calls to Google Calendar and the Telegram
    /// gateway (api/Outbound.hpp): 4 threads, 32 waiting tasks, apart from
    /// shared() so a hung gateway does not hold the Xiaomi calls and the
    /// voice, nor they these. Never destroyed, as shared().
    static BlockingPool& outbound();

private:
    void ensure_started_();  // called under mutex_
    void run_();

    const std::size_t threads_;
    const std::size_t max_queued_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Task> queue_;
    std::vector<std::thread> workers_;
    std::size_t running_ = 0;
    bool stopping_ = false;
};

}  // namespace Utils
