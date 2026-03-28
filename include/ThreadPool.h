#include "BlockingQueue.h"
#include <cmath>
#include <cstddef>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>
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
    
    // 返回结果给调用方
    template<class F, class... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>
    {
        using ReturnType = std::invoke_result_t<F, Args...>;
    
        auto bound = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::move(bound));
        std::future<ReturnType> fut = task->get_future();
        
        task_queue_.push( [task]() { (*task)(); } );

        return fut;
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