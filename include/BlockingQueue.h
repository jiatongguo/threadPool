#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
namespace tp {
template<typename T>
class BlockingQueue
{
public:
    explicit BlockingQueue(std::size_t cap) : cap_(cap) {}

    bool push(const T& value);
    bool push(T&& value);
    bool pop(T& out);
    void close();
    
private:
    std::queue<T> q_;
    std::size_t cap_;
    std::mutex mtx_;
    bool closed_ {false};
    std::condition_variable not_empty_; // 可以pop
    std::condition_variable not_full_; // 可以push
};

}