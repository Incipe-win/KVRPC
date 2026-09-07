#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace kvrpc {
struct ServerOptions {
    size_t io_threads = 2;
    size_t workers = 4;
    size_t queue_capacity = 1024;
    size_t queue_bytes = 64 * 1024 * 1024;
    size_t output_bytes = 8 * 1024 * 1024;
    size_t pipeline = 16;
    std::chrono::milliseconds idle_timeout{30000};
};
class TcpServer {
   public:
    struct Reply {
        size_t consumed = 0;
        std::vector<uint8_t> bytes;
        bool close = false;
    };
    using Input = std::reference_wrapper<const std::vector<uint8_t>>;
    using Handler = std::function<std::vector<uint8_t>(const std::vector<uint8_t>&, size_t&)>;
    using BatchHandler = std::function<std::vector<Reply>(const std::vector<Input>&)>;
    using FrameSizer = std::function<size_t(const uint8_t*, size_t)>;
    explicit TcpServer(int port, std::string bind_address = "127.0.0.1", size_t max_connections = 4096,
                       size_t max_frame_bytes = 2 * 1024 * 1024 + 4);
    ~TcpServer();
    void start();
    uint16_t port() const noexcept { return bound_port_.load(); }
    void stop() noexcept;
    void configure(ServerOptions options);
    void setFrameSizer(FrameSizer sizer) { sizer_ = std::move(sizer); }
    void setHandler(Handler handler) {
        handler_ = std::move(handler);
        batch_handler_ = {};
    }
    void setBatchHandler(BatchHandler handler) {
        batch_handler_ = std::move(handler);
        handler_ = {};
    }
    struct Stats {
        uint64_t accepted, active, rejected, requests, timeouts, queued_tasks, queued_bytes, handler_us, handler_errors;
    };
    Stats stats() const;

   private:
    struct Runtime;
    int port_;
    std::string bind_address_;
    size_t max_connections_, max_frame_bytes_;
    ServerOptions options_;
    std::atomic<bool> stopping_{false};
    std::atomic<uint16_t> bound_port_{0};
    Handler handler_;
    BatchHandler batch_handler_;
    FrameSizer sizer_;
    std::mutex runtime_mutex_;
    std::shared_ptr<Runtime> runtime_;
    std::atomic<uint64_t> queued_tasks_{0}, queued_bytes_{0}, handler_us_{0}, handler_errors_{0};
    std::atomic<uint64_t> accepted_{0}, active_{0}, rejected_{0}, requests_{0}, timeouts_{0};
};
}  // namespace kvrpc
