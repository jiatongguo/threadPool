#include "ThreadPool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <stdexcept>
#include <thread>
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

TEST(ThreadPoolTest, RejectsZeroWorkerCount)
{
    try
    {
        (void)tp::ThreadPool(0, 1);
        FAIL() << "Expected std::invalid_argument";
    }
    catch (const std::invalid_argument& ex)
    {
        EXPECT_STREQ(ex.what(), "线程数量必须大于0");
    }
    catch (...)
    {
        FAIL() << "Expected std::invalid_argument";
    }
}

TEST(ThreadPoolTest, RejectsZeroQueueCapacity)
{
    try
    {
        (void)tp::ThreadPool(1, 0);
        FAIL() << "Expected std::invalid_argument";
    }
    catch (const std::invalid_argument& ex)
    {
        EXPECT_STREQ(ex.what(), "任务队列容量必须大于0");
    }
    catch (...)
    {
        FAIL() << "Expected std::invalid_argument";
    }
}

TEST(BlockingQueueTest, RejectsZeroCapacity)
{
    try
    {
        (void)tp::BlockingQueue<int>(0);
        FAIL() << "Expected std::invalid_argument";
    }
    catch (const std::invalid_argument& ex)
    {
        EXPECT_STREQ(ex.what(), "任务队列容量必须大于0");
    }
    catch (...)
    {
        FAIL() << "Expected std::invalid_argument";
    }
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

TEST(ThreadPoolTest, MultipleProducersSubmitConcurrently)
{
    tp::ThreadPool pool(4, 32);
    constexpr int producer_count = 4;
    constexpr int tasks_per_producer = 25;
    constexpr int total_tasks = producer_count * tasks_per_producer;
    std::promise<void> start;
    auto start_future = start.get_future().share();
    std::vector<std::thread> producers;
    std::vector<std::vector<std::future<int>>> futures(producer_count);

    for (int producer = 0; producer < producer_count; ++producer)
    {
        producers.emplace_back([&, producer] {
            start_future.wait();
            futures[producer].reserve(tasks_per_producer);

            for (int task = 0; task < tasks_per_producer; ++task)
            {
                const int submission_id = producer * tasks_per_producer + task;
                futures[producer].push_back(pool.submit([submission_id] {
                    return submission_id;
                }));
            }
        });
    }

    start.set_value();

    for (auto& producer : producers)
    {
        producer.join();
    }

    std::vector<int> seen(total_tasks, 0);

    for (auto& producer_futures : futures)
    {
        for (auto& future : producer_futures)
        {
            ++seen[future.get()];
        }
    }

    for (int count : seen)
    {
        EXPECT_EQ(count, 1);
    }
}

TEST(ThreadPoolTest, RepeatedCreateSubmitAndDestroyRemainsStable)
{
    constexpr int round_count = 40;
    constexpr int tasks_per_round = 64;
    std::atomic<int> completed {0};

    for (int round = 0; round < round_count; ++round)
    {
        {
            tp::ThreadPool pool(4, 64);
            std::vector<std::future<void>> futures;
            futures.reserve(tasks_per_round);

            for (int task = 0; task < tasks_per_round; ++task)
            {
                futures.push_back(pool.submit([&completed] {
                    completed.fetch_add(1, std::memory_order_relaxed);
                }));
            }

            for (auto& future : futures)
            {
                future.get();
            }
        }
    }

    EXPECT_EQ(completed.load(std::memory_order_relaxed), round_count * tasks_per_round);
}

TEST(ThreadPoolTest, ShutdownRejectsNewTasks)
{
    // Once shutdown starts, submit() should fail immediately for new work.
    tp::ThreadPool pool(1, 4);

    pool.shutdown();

    try
    {
        (void)pool.submit([] { return 1; });
        FAIL() << "Expected std::runtime_error";
    }
    catch (const std::runtime_error& ex)
    {
        EXPECT_STREQ(ex.what(), "线程池已停止接收新任务");
    }
    catch (...)
    {
        FAIL() << "Expected std::runtime_error";
    }
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

TEST(ThreadPoolTest, ShutdownRejectsBlockedConcurrentSubmitters)
{
    tp::ThreadPool pool(1, 1);
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

    auto second = pool.submit([&] {
        second_ran.store(true, std::memory_order_release);
    });

    EXPECT_EQ(first_started_future.wait_for(500ms), std::future_status::ready);

    std::vector<std::future<bool>> submitters;
    for (int i = 0; i < 4; ++i)
    {
        submitters.push_back(std::async(std::launch::async, [&pool] {
            try
            {
                auto submitted = pool.submit([] { return 1; });
                submitted.get();
                return true;
            }
            catch (const std::runtime_error&)
            {
                return false;
            }
        }));
    }

    for (auto& submitter : submitters)
    {
        EXPECT_EQ(submitter.wait_for(100ms), std::future_status::timeout);
    }

    auto shutdown_done = std::async(std::launch::async, [&pool, &shutdown_started] {
        shutdown_started.set_value();
        pool.shutdown(tp::ThreadPool::ShutdownMode::Graceful);
    });

    EXPECT_EQ(shutdown_started_future.wait_for(500ms), std::future_status::ready);
    EXPECT_EQ(shutdown_done.wait_for(0ms), std::future_status::timeout);

    for (auto& submitter : submitters)
    {
        EXPECT_EQ(submitter.wait_for(500ms), std::future_status::ready);
        EXPECT_FALSE(submitter.get());
    }

    release_first.set_value();

    first.get();
    second.get();
    EXPECT_EQ(shutdown_done.wait_for(500ms), std::future_status::ready);
    EXPECT_TRUE(second_ran.load(std::memory_order_acquire));
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
