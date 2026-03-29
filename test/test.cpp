#include "ThreadPool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <stdexcept>
#include <vector>

namespace {

using namespace std::chrono_literals;

TEST(ThreadPoolTest, SubmitReturnsFutureResult)
{
    // Basic submit path: task result should be propagated through the future.
    tp::ThreadPool pool(2, 8);

    auto result = pool.submit([](int a, int b) {
        return a + b;
    }, 1, 2);

    EXPECT_EQ(result.get(), 3);
}

TEST(ThreadPoolTest, ExecutesAllSubmittedTasks)
{
    // Multiple tasks should all run exactly once even when workers consume them concurrently.
    tp::ThreadPool pool(4, 32);
    std::atomic<int> sum {0};
    std::vector<std::future<void>> futures;

    for (int i = 1; i <= 16; ++i)
    {
        futures.push_back(pool.submit([&sum, i] {
            sum.fetch_add(i, std::memory_order_relaxed);
        }));
    }

    for (auto& future : futures)
    {
        future.get();
    }

    EXPECT_EQ(sum.load(std::memory_order_relaxed), 136);
}

TEST(ThreadPoolTest, ShutdownRejectsNewTasks)
{
    // Once shutdown starts, submit() should fail immediately for new work.
    tp::ThreadPool pool(1, 4);

    pool.shutdown();

    EXPECT_THROW(pool.submit([] { return 1; }), std::runtime_error);
}

TEST(ThreadPoolTest, TaskExceptionsReachFuture)
{
    // packaged_task should capture task exceptions and surface them via future.get().
    tp::ThreadPool pool(1, 4);

    auto result = pool.submit([]() -> int {
        throw std::runtime_error("boom");
    });

    try
    {
        (void)result.get();
        FAIL() << "Expected std::runtime_error";
    }
    catch (const std::runtime_error& ex)
    {
        EXPECT_STREQ(ex.what(), "boom");
    }
    catch (...)
    {
        FAIL() << "Expected std::runtime_error";
    }
}

TEST(ThreadPoolTest, SubmitBlocksWhenQueueIsFull)
{
    // With one worker and queue capacity one, the third submit should block until space frees up.
    tp::ThreadPool pool(1, 1);
    std::promise<void> first_started;
    std::promise<void> release_first;
    std::promise<void> second_ran;
    auto first_started_future = first_started.get_future();
    auto release_first_future = release_first.get_future().share();
    auto second_ran_future = second_ran.get_future();

    auto first = pool.submit([&] {
        first_started.set_value();
        release_first_future.wait();
    });

    EXPECT_EQ(first_started_future.wait_for(500ms), std::future_status::ready);

    auto second = pool.submit([&] {
        second_ran.set_value();
    });

    auto third_submit_done = std::async(std::launch::async, [&pool] {
        auto third = pool.submit([] {});
        third.get();
    });

    // The queue is full until the first task is released and the worker can pop the second task.
    EXPECT_EQ(third_submit_done.wait_for(100ms), std::future_status::timeout);

    release_first.set_value();

    first.get();
    second.get();
    EXPECT_EQ(second_ran_future.wait_for(500ms), std::future_status::ready);
    EXPECT_EQ(third_submit_done.wait_for(500ms), std::future_status::ready);
}

TEST(ThreadPoolTest, ShutdownIsIdempotent)
{
    // Repeated shutdown calls should be harmless after the first successful transition.
    tp::ThreadPool pool(2, 8);

    auto result = pool.submit([] {
        return 42;
    });

    EXPECT_EQ(result.get(), 42);

    pool.shutdown();
    EXPECT_NO_THROW(pool.shutdown(tp::ThreadPool::ShutdownMode::Immediate));
}

TEST(ThreadPoolTest, GracefulShutdownCompletesQueuedTasks)
{
    // In graceful mode, shutdown waits for the currently running task and the queued task.
    tp::ThreadPool pool(1, 4);
    std::promise<void> first_started;
    std::promise<void> release_first;
    std::promise<void> shutdown_started;
    std::atomic<bool> second_ran {false};
    auto first_started_future = first_started.get_future();
    auto release_first_future = release_first.get_future().share();
    auto shutdown_started_future = shutdown_started.get_future();

    auto first = pool.submit([&] {
        first_started.set_value();
        release_first_future.wait();
    });

    // This task must remain queued until the first task is released.
    auto second = pool.submit([&] {
        second_ran.store(true, std::memory_order_release);
    });

    EXPECT_EQ(first_started_future.wait_for(500ms), std::future_status::ready);

    auto shutdown_done = std::async(std::launch::async, [&pool, &shutdown_started] {
        shutdown_started.set_value();
        pool.shutdown(tp::ThreadPool::ShutdownMode::Graceful);
    });

    // shutdown() should block here because work is still pending.
    EXPECT_EQ(shutdown_started_future.wait_for(500ms), std::future_status::ready);
    EXPECT_EQ(shutdown_done.wait_for(0ms), std::future_status::timeout);

    release_first.set_value();

    first.get();
    second.get();
    EXPECT_EQ(shutdown_done.wait_for(500ms), std::future_status::ready);
    EXPECT_TRUE(second_ran.load(std::memory_order_acquire));
}

TEST(ThreadPoolTest, DestructorRunsGracefulShutdown)
{
    // The destructor delegates to graceful shutdown, so queued work should still complete.
    auto pool = std::make_unique<tp::ThreadPool>(1, 4);
    std::promise<void> first_started;
    std::promise<void> release_first;
    std::atomic<bool> queued_task_ran {false};
    auto first_started_future = first_started.get_future();
    auto release_first_future = release_first.get_future().share();

    auto first = pool->submit([&] {
        first_started.set_value();
        release_first_future.wait();
    });

    auto second = pool->submit([&] {
        queued_task_ran.store(true, std::memory_order_release);
    });

    EXPECT_EQ(first_started_future.wait_for(500ms), std::future_status::ready);

    auto destroy_done = std::async(std::launch::async, [&pool] {
        pool.reset();
    });

    EXPECT_EQ(destroy_done.wait_for(0ms), std::future_status::timeout);

    release_first.set_value();

    EXPECT_EQ(destroy_done.wait_for(500ms), std::future_status::ready);
    first.get();
    second.get();
    EXPECT_TRUE(queued_task_ran.load(std::memory_order_acquire));
}

TEST(ThreadPoolTest, ImmediateShutdownDiscardsQueuedTasks)
{
    // In immediate mode, shutdown still waits for the running task but drops queued work.
    tp::ThreadPool pool(1, 4);
    std::promise<void> first_started;
    std::promise<void> release_first;
    std::promise<void> shutdown_started;
    std::atomic<bool> queued_task_ran {false};
    auto first_started_future = first_started.get_future();
    auto release_first_future = release_first.get_future().share();
    auto shutdown_started_future = shutdown_started.get_future();

    auto running = pool.submit([&] {
        first_started.set_value();
        release_first_future.wait();
    });

    // This task should be discarded once immediate shutdown clears the queue.
    pool.submit([&] {
        queued_task_ran.store(true, std::memory_order_release);
    });

    EXPECT_EQ(first_started_future.wait_for(500ms), std::future_status::ready);

    auto shutdown_done = std::async(std::launch::async, [&pool, &shutdown_started] {
        shutdown_started.set_value();
        pool.shutdown(tp::ThreadPool::ShutdownMode::Immediate);
    });

    // shutdown() should block until the running task exits.
    EXPECT_EQ(shutdown_started_future.wait_for(500ms), std::future_status::ready);
    EXPECT_EQ(shutdown_done.wait_for(0ms), std::future_status::timeout);

    release_first.set_value();
    running.get();

    EXPECT_EQ(shutdown_done.wait_for(500ms), std::future_status::ready);
    EXPECT_FALSE(queued_task_ran.load(std::memory_order_acquire));
}

}  // namespace
