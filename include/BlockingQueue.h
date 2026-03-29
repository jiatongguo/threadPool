#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <utility>
namespace tp {
inline constexpr char kInvalidQueueCapacityMessage[] = "任务队列容量必须大于0";

template<typename T>
class BlockingQueue
{
public:
    explicit BlockingQueue(std::size_t cap) : cap_(cap)
    {
        if (cap_ == 0)
        {
            throw std::invalid_argument(kInvalidQueueCapacityMessage);
        }
    }

    bool push(const T& value);
    bool push(T&& value);
    bool pop(T& out);
    void close();
    void clear();
    
private:
    std::queue<T> q_;
    std::size_t cap_;
    std::mutex mtx_;
    bool closed_ {false};
    std::condition_variable not_empty_; // 可以pop
    std::condition_variable not_full_; // 可以push
};

template<typename T>
bool BlockingQueue<T>::push(const T& value) 
{
    std::unique_lock<std::mutex> lock(mtx_);
    not_full_.wait(lock, [this]
            { return closed_ || q_.size() < cap_ ;});
 
    if (closed_)
    {
        return false;
    }

    q_.push(value);
    not_empty_.notify_one();

    return true;
}

template<typename T>
bool BlockingQueue<T>::push(T&& value)
{
    std::unique_lock<std::mutex> lock(mtx_);
    not_full_.wait(lock, [this]
            { return closed_ || q_.size() < cap_ ;});
 
    if (closed_)
    {
        return false;
    }

    q_.push(std::move(value));
    not_empty_.notify_one();
    
    return true;
}

template<typename T>
bool BlockingQueue<T>::pop(T& out) 
{
    std::unique_lock<std::mutex> lock(mtx_);
    not_empty_.wait(lock, [this] 
        { return closed_ || !q_.empty(); });
    
    if (closed_ && q_.empty())
    {
        return false;
    }

    out = q_.front();
    q_.pop();

    not_full_.notify_one();

    return true;
}

template<typename T>
void BlockingQueue<T>::close() 
{
    std::lock_guard<std::mutex> lock(mtx_);
    closed_ = true;

    not_empty_.notify_all();
    not_full_.notify_all();
}

template<typename T>
void BlockingQueue<T>::clear()
{
    std::lock_guard<std::mutex> lock(mtx_);
    while (!q_.empty())
    {
        q_.pop();
    }
    not_full_.notify_all();
}

}
