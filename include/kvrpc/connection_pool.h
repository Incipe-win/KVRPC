#pragma once
#include <arpa/inet.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#include "kvrpc/error.h"
#include "kvrpc/tcp_connection.h"

namespace kvrpc {
class ConnectionPool {
    struct State {
        std::mutex mutex;
        std::condition_variable ready;
        std::vector<std::unique_ptr<TcpConnection>> free;
        bool closed = false;
    };

   public:
    ConnectionPool(const std::string& ip, uint16_t port, size_t size, TransportOptions options = {},
                   std::chrono::milliseconds acquire_timeout = std::chrono::seconds(5))
        : state_(std::make_shared<State>()),
          ip_(ip),
          port_(port),
          acquire_timeout_(acquire_timeout),
          slots_(size),
          options_(options) {
        in_addr address{};
        if (!size || size > 4096 || !port || inet_pton(AF_INET, ip.c_str(), &address) != 1 ||
            acquire_timeout.count() <= 0 || acquire_timeout > std::chrono::hours(24))
            throw Error(ErrorCode::invalid_argument, "Invalid pool size, IPv4 address, port, or acquire timeout");
        state_->free.reserve(size);
        for (size_t i = 0; i < size; ++i) state_->free.emplace_back(std::make_unique<TcpConnection>(options));
    }
    ClientEndpoint Endpoint() const { return {ip_, port_, slots_, options_}; }
    const std::string& Address() const { return ip_; }
    uint16_t Port() const { return port_; }
    size_t Slots() const { return slots_; }
    TransportOptions Options() const { return options_; }
    ~ConnectionPool() { Close(); }
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;
    void Close() noexcept {
        auto state = state_;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->closed = true;
            state->free.clear();
        }
        state->ready.notify_all();
    }
    std::shared_ptr<TcpConnection> Acquire() {
        auto state = state_;
        std::unique_lock<std::mutex> lock(state->mutex);
        if (!state->ready.wait_for(lock, acquire_timeout_, [&] { return state->closed || !state->free.empty(); }))
            throw Error(ErrorCode::timeout, "Connection pool acquisition timed out");
        if (state->closed) throw Error(ErrorCode::closed, "Connection pool is closed");
        auto owned = std::move(state->free.back());
        state->free.pop_back();
        lock.unlock();
        // The lease owns State, never a raw pointer to the ConnectionPool wrapper.
        std::shared_ptr<TcpConnection> conn(owned.release(), [state](TcpConnection* p) noexcept {
            std::unique_ptr<TcpConnection> returned(p);
            std::lock_guard<std::mutex> guard(state->mutex);
            if (!state->closed) state->free.push_back(std::move(returned));
            state->ready.notify_one();
        });
        if (!conn->IsConnected() && !conn->Connect(ip_, port_))
            throw Error(conn->TimedOut() ? ErrorCode::timeout : ErrorCode::connection, "Unable to connect to server");
        conn->BeginRequest();
        return conn;
    }

   private:
    std::shared_ptr<State> state_;
    std::string ip_;
    uint16_t port_;
    std::chrono::milliseconds acquire_timeout_;
    size_t slots_;
    TransportOptions options_;
};
}  // namespace kvrpc
