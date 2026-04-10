#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "thread_pool.h"

namespace kvcache {

struct Connection {
    int fd;
    std::vector<uint8_t> read_buffer;
    std::mutex mutex;  // Protect buffer
};

class TcpServer {
public:
    // Handler takes raw bytes and returns response bytes
    // It also returns how many bytes were consumed. If 0, it means we need more
    // data.
    using Handler = std::function<std::vector<uint8_t>(const std::vector<uint8_t>&, size_t&)>;

    TcpServer(int port, int thread_pool_size = 4);
    ~TcpServer();

    void start();
    void stop();
    void setHandler(Handler handler);

private:
    int port_;
    int server_fd_;
    int epoll_fd_;
    std::atomic<bool> running_;
    ThreadPool thread_pool_;
    Handler handler_;

    std::mutex connections_mutex_;
    std::unordered_map<int, std::shared_ptr<Connection>> connections_;

    void eventLoop();
    void handleNewConnection();
    void handleClientData(int client_fd);
    void setNonBlocking(int fd);
    void removeConnection(int fd);
};

}  // namespace kvcache
