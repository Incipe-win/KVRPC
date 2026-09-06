#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kvrpc {
// One event loop owns all descriptors. Configure handlers before start().
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
    explicit TcpServer(int port, std::string bind_address = "127.0.0.1", size_t max_connections = 128,
                       size_t max_frame_bytes = 2 * 1024 * 1024 + 4);
    void start();
    uint16_t port() const noexcept { return bound_port_.load(); }
    void stop() noexcept { stopping_ = true; }
    void setHandler(Handler handler) { handler_ = std::move(handler); batch_handler_ = {}; }
    void setBatchHandler(BatchHandler handler) { batch_handler_ = std::move(handler); handler_ = {}; }
private:
    int port_;
    std::string bind_address_;
    size_t max_connections_, max_frame_bytes_;
    std::atomic<bool> stopping_{false};
    std::atomic<uint16_t> bound_port_{0};
    Handler handler_;
    BatchHandler batch_handler_;
};
}  // namespace kvrpc
