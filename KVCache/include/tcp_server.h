#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kvcache {
class TcpServer {
public:
    using Handler = std::function<std::vector<uint8_t>(const std::vector<uint8_t>&, size_t&)>;
    explicit TcpServer(int port, std::string bind_address = "127.0.0.1", size_t max_connections = 128);
    // start blocks; call stop and join the start thread before destroying the server.
    void start();
    void stop() noexcept { stopping_ = true; }
    void setHandler(Handler handler) { handler_ = std::move(handler); }
private:
    int port_;
    std::string bind_address_;
    size_t max_connections_;
    std::atomic<bool> stopping_{false};
    Handler handler_;
};
}  // namespace kvcache
