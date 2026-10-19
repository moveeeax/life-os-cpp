/**
 * @file test_blocking_pool.cpp
 * @brief Utils::BlockingPool: work runs off the caller's thread, the number of
 *        running and waiting tasks is bounded, a throwing task does not take a
 *        thread down.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

#include "utils/BlockingPool.hpp"

namespace {

/// A gate the test opens: tasks that wait on it hold their thread.
class Gate {
public:
    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return open_; });
    }
    void open() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            open_ = true;
        }
        cv_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool open_ = false;
};

}  // namespace

TEST(BlockingPool, RunsTheTaskOnAnotherThread) {
    Utils::BlockingPool pool(2, 2);
    std::promise<std::thread::id> ran;
    auto ran_on = ran.get_future();
    ASSERT_TRUE(pool.try_submit([&ran] { ran.set_value(std::this_thread::get_id()); }));
    ASSERT_EQ(ran_on.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_NE(ran_on.get(), std::this_thread::get_id());
}

TEST(BlockingPool, RefusesOnceThreadsAndQueueAreTaken) {
    Utils::BlockingPool pool(1, 1);
    Gate gate;
    std::atomic<int> done{0};
    const auto blocked = [&] {
        gate.wait();
        ++done;
    };
    ASSERT_TRUE(pool.try_submit(blocked));  // runs and holds the only thread
    ASSERT_TRUE(pool.try_submit(blocked));  // waits in the queue
    EXPECT_FALSE(pool.try_submit(blocked)) << "a third task must be refused at once";
    EXPECT_EQ(pool.in_flight(), 2u);

    gate.open();
    for (int i = 0; i < 200 && pool.in_flight() > 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(done.load(), 2);
    EXPECT_EQ(pool.in_flight(), 0u);
    // Room again once the work is done.
    std::promise<void> again;
    auto again_done = again.get_future();
    EXPECT_TRUE(pool.try_submit([&again] { again.set_value(); }));
    EXPECT_EQ(again_done.wait_for(std::chrono::seconds(10)), std::future_status::ready);
}

TEST(BlockingPool, AThrowingTaskDoesNotStopThePool) {
    Utils::BlockingPool pool(1, 4);
    ASSERT_TRUE(pool.try_submit([] { throw std::runtime_error("boom"); }));
    std::promise<void> next;
    auto next_done = next.get_future();
    ASSERT_TRUE(pool.try_submit([&next] { next.set_value(); }));
    EXPECT_EQ(next_done.wait_for(std::chrono::seconds(10)), std::future_status::ready);
}

TEST(BlockingPool, DestructionRunsWhatIsQueuedAndJoins) {
    std::atomic<int> done{0};
    {
        Utils::BlockingPool pool(1, 8);
        for (int i = 0; i < 5; ++i) {
            ASSERT_TRUE(pool.try_submit([&done] {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                ++done;
            }));
        }
    }
    EXPECT_EQ(done.load(), 5);
}
