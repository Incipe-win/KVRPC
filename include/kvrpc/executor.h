#pragma once
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "kvrpc/error.h"

namespace kvrpc {
struct ClientOptions {
    size_t workers = 4;
    size_t queue_capacity = 64;
    size_t max_frame_bytes = 2 * 1024 * 1024;
    size_t queue_bytes = 64 * 1024 * 1024;
    size_t max_inflight = 16;
};
// Destruction drains admitted work and joins workers. Submit never waits for queue space.
class Executor {
   public:
    explicit Executor(ClientOptions options = {}) : capacity_(options.queue_capacity) {
        if (!options.workers || options.workers > 256 || !capacity_ || capacity_ > 65536 || !options.max_frame_bytes ||
            options.max_frame_bytes > 64 * 1024 * 1024)
            throw Error(ErrorCode::invalid_argument, "Invalid client resource limits");
        try {
            for (size_t i = 0; i < options.workers; ++i) workers_.emplace_back([this] { Run(); });
        } catch (...) {
            Stop();
            throw;
        }
    }
    ~Executor() { Stop(); }
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;
    template <class F>
    auto Submit(F&& function) -> std::future<std::invoke_result_t<F>> {
        using Result = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(function));
        auto result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || tasks_.size() >= capacity_) {
                std::promise<Result> rejected;
                rejected.set_exception(std::make_exception_ptr(
                    Error(stopping_ ? ErrorCode::closed : ErrorCode::overloaded, "Client queue is closed or full")));
                return rejected.get_future();
            }
            tasks_.emplace([task] { (*task)(); });
        }
        ready_.notify_one();
        return result;
    }

   private:
    void Stop() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        for (auto& worker : workers_)
            if (worker.joinable()) worker.join();
    }
    void Run() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                if (tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }
    size_t capacity_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::queue<std::function<void()>> tasks_;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};
}  // namespace kvrpc
