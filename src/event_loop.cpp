#include "kvrpc/event_loop.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <system_error>

namespace kvrpc {
namespace {
void Fail(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}
}  // namespace
EventLoop::EventLoop() {
    epoll_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_ < 0) Fail("epoll_create1");
    wake_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_ < 0) {
        close(epoll_);
        Fail("eventfd");
    }
    epoll_event event{};
    event.events = EPOLLIN | EPOLLET;
    event.data.u64 = 0;
    if (epoll_ctl(epoll_, EPOLL_CTL_ADD, wake_, &event) < 0) {
        close(wake_);
        close(epoll_);
        Fail("epoll_ctl wakeup");
    }
}
EventLoop::~EventLoop() {
    close(wake_);
    close(epoll_);
}
uint64_t EventLoop::Add(int fd, uint32_t events, std::function<void(uint32_t)> callback) {
    auto id = next_++;
    epoll_event event{};
    event.events = events | EPOLLET;
    event.data.u64 = id;
    if (epoll_ctl(epoll_, EPOLL_CTL_ADD, fd, &event) < 0) Fail("epoll_ctl add");
    watches_.emplace(id, Watch{fd, std::move(callback)});
    return id;
}
void EventLoop::Modify(uint64_t token, uint32_t events) {
    auto it = watches_.find(token);
    if (it == watches_.end()) return;
    epoll_event event{};
    event.events = events | EPOLLET;
    event.data.u64 = token;
    if (epoll_ctl(epoll_, EPOLL_CTL_MOD, it->second.fd, &event) < 0) Fail("epoll_ctl modify");
}
void EventLoop::Remove(uint64_t token) {
    auto it = watches_.find(token);
    if (it == watches_.end()) return;
    epoll_ctl(epoll_, EPOLL_CTL_DEL, it->second.fd, nullptr);
    watches_.erase(it);
}
uint64_t EventLoop::At(Clock::time_point deadline, Task task) {
    auto id = next_++;
    timer_index_[id] = timers_.emplace(deadline, std::make_pair(id, std::move(task)));
    return id;
}
void EventLoop::Cancel(uint64_t id) {
    auto it = timer_index_.find(id);
    if (it == timer_index_.end()) return;
    timers_.erase(it->second);
    timer_index_.erase(it);
}
void EventLoop::Post(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    uint64_t one = 1;
    while (write(wake_, &one, sizeof(one)) < 0 && errno == EINTR) {
    }
}
void EventLoop::Stop() {
    stopping_ = true;
    Post([] {});
}
void EventLoop::Run() {
    epoll_event events[256];
    while (!stopping_) {
        int timeout = -1;
        if (!timers_.empty()) {
            auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(timers_.begin()->first - Clock::now()).count();
            timeout = static_cast<int>(std::clamp<int64_t>(ms + 1, 0, INT_MAX));
        }
        int count = epoll_wait(epoll_, events, 256, timeout);
        if (count < 0) {
            if (errno == EINTR) continue;
            Fail("epoll_wait");
        }
        for (int i = 0; i < count; ++i) {
            if (!events[i].data.u64) {
                uint64_t value;
                while (read(wake_, &value, sizeof(value)) > 0 || errno == EINTR) {
                }
                std::vector<Task> tasks;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    tasks.swap(tasks_);
                }
                for (auto& task : tasks) task();
            } else {
                auto it = watches_.find(events[i].data.u64);
                if (it != watches_.end()) {
                    auto callback = it->second.callback;
                    callback(events[i].events);
                }
            }
        }
        while (!timers_.empty() && timers_.begin()->first <= Clock::now()) {
            auto it = timers_.begin();
            auto item = std::move(it->second);
            timer_index_.erase(item.first);
            timers_.erase(it);
            item.second();
        }
    }
}
}  // namespace kvrpc
