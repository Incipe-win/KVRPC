#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace kvrpc {
// Linux ET reactor. Descriptor callbacks and timers run on its owning thread.
class EventLoop {
   public:
    using Clock = std::chrono::steady_clock;
    using Task = std::function<void()>;
    EventLoop();
    ~EventLoop();
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    uint64_t Add(int fd, uint32_t events, std::function<void(uint32_t)> callback);
    void Modify(uint64_t token, uint32_t events);
    void Remove(uint64_t token);
    uint64_t At(Clock::time_point deadline, Task task);
    void Cancel(uint64_t timer);
    void Post(Task task);
    void Run();
    void Stop();

   private:
    struct Watch {
        int fd;
        std::function<void(uint32_t)> callback;
    };
    using Timers = std::multimap<Clock::time_point, std::pair<uint64_t, Task>>;
    int epoll_ = -1, wake_ = -1;
    uint64_t next_ = 1;
    std::unordered_map<uint64_t, Watch> watches_;
    Timers timers_;
    std::unordered_map<uint64_t, Timers::iterator> timer_index_;
    std::mutex mutex_;
    std::vector<Task> tasks_;
    std::atomic<bool> stopping_{false};
};
}  // namespace kvrpc
