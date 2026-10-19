/**
 * @file BlockingPool.cpp
 * @brief Bodies for src/utils/BlockingPool.hpp — compiled once into app_core.
 */

#include "utils/BlockingPool.hpp"

#include <exception>
#include <utility>

#include <spdlog/spdlog.h>

namespace Utils {

BlockingPool::BlockingPool(std::size_t threads, std::size_t max_queued)
    : threads_(threads == 0 ? 1 : threads), max_queued_(max_queued) {}

BlockingPool::~BlockingPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

bool BlockingPool::try_submit(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || running_ + queue_.size() >= threads_ + max_queued_) {
            return false;
        }
        ensure_started_();
        queue_.push_back(std::move(task));
    }
    ready_.notify_one();
    return true;
}

std::size_t BlockingPool::in_flight() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_ + queue_.size();
}

BlockingPool& BlockingPool::shared() {
    static auto* pool = new BlockingPool(4, 16);
    return *pool;
}

void BlockingPool::ensure_started_() {
    if (!workers_.empty()) {
        return;
    }
    workers_.reserve(threads_);
    for (std::size_t i = 0; i < threads_; ++i) {
        workers_.emplace_back([this] { run_(); });
    }
}

void BlockingPool::run_() {
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) {
                return;  // stopping and nothing left to run
            }
            task = std::move(queue_.front());
            queue_.pop_front();
            ++running_;
        }
        try {
            task();
        } catch (const std::exception& e) {
            spdlog::error("blocking pool task failed: {}", e.what());
        } catch (...) {
            spdlog::error("blocking pool task failed with a non-std exception");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        --running_;
    }
}

}  // namespace Utils
