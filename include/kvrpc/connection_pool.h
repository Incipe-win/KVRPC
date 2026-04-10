#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

#include "kvrpc/tcp_connection.h"

namespace kvrpc {

class ConnectionPool {
   private:
    std::string ip_;
    uint16_t port_;
    size_t pool_size_;

    std::queue<TcpConnection*> free_connections_;
    std::mutex mutex_;
    std::condition_variable cond_;

   public:
    ConnectionPool(const std::string& ip, uint16_t port, size_t pool_size)
        : ip_(ip), port_(port), pool_size_(pool_size) {
        // Initialize the pool with pre-connected sockets (or lazily connected)
        // Here we initialize them lazy or proactively. Let's do lazy loading inside
        // Acquire. But for pool budgeting, we just push `pool_size` empty
        // connections.
        for (size_t i = 0; i < pool_size_; ++i) {
            free_connections_.push(new TcpConnection());
        }
    }

    ~ConnectionPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!free_connections_.empty()) {
            TcpConnection* conn = free_connections_.front();
            free_connections_.pop();
            delete conn;
        }
    }

    // Returns a connection wrapped in a shared_ptr with a custom deleter
    // The custom deleter returns the connection back to the pool!
    std::shared_ptr<TcpConnection> Acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this]() { return !free_connections_.empty(); });

        TcpConnection* conn = free_connections_.front();
        free_connections_.pop();
        lock.unlock();  // drop lock while connecting

        if (!conn->IsConnected()) {
            // Lazy connect. If it fails, we still return it but it's disconnected.
            // Client should handle the failure.
            conn->Connect(ip_, port_);
        }

        // Custom deleter: return to pool
        return std::shared_ptr<TcpConnection>(conn, [this](TcpConnection* p) { this->Release(p); });
    }

   private:
    void Release(TcpConnection* conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        free_connections_.push(conn);
        cond_.notify_one();
    }
};

}  // namespace kvrpc
