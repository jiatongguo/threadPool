#include "BlockingQueue.h"
#include <cstddef>
#include <functional>
#include <iterator>
#include <utility>
#include <vector>
#include <thread>
#include <atomic>

namespace tp {
class ThreadPool
{
public:
    explicit ThreadPool(std::size_t numsThread, std::size_t cap) 
    : task_queue_(cap)
    {
        for (std::size_t i = 0; i < numsThread; ++i) 
        {
            workers_.emplace_back([this] 
            {
                worker_loop();
            });
        }
    }

    ~ThreadPool()
    {
        stop_.store(true);
        task_queue_.close();

        for (auto& worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }
    
    void enqueue(std::function<void()> task)
    {
        task_queue_.push(std::move(task));
    }

private:
    void worker_loop() noexcept
    {
        std::function<void()> task;
        while (!stop_ && task_queue_.pop(task)) 
        {
            try
            {
                task();
            }
            catch(...)
            {

            }
        }
    }
private:
    std::vector<std::thread> workers_;
    BlockingQueue<std::function<void()>> task_queue_;
    std::atomic<bool> stop_ {false};
};

}