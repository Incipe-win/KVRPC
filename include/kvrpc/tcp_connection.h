#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace kvrpc {
struct TransportOptions {
    std::chrono::milliseconds connect_timeout{2000};
    std::chrono::milliseconds io_timeout{5000};
};

// Exclusive ownership: a connection must be used by only one request at a time.
class TcpConnection {
public:
    explicit TcpConnection(TransportOptions options = {});
    ~TcpConnection() { Close(); }
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    bool Connect(const std::string& ip, uint16_t port);
    bool SendAll(const char* data, size_t len);
    bool RecvAll(char* buffer, size_t len);
    void BeginRequest();
    void Close() noexcept;
    bool IsConnected() const noexcept { return fd_ >= 0; }
    bool TimedOut() const noexcept { return timed_out_; }
private:
    bool Wait(short events, std::chrono::steady_clock::time_point deadline);
    int fd_ = -1;
    TransportOptions options_;
    std::chrono::steady_clock::time_point deadline_;
    bool timed_out_ = false;
};
}  // namespace kvrpc
