#include "tcp_server.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <iostream>
#include <vector>

namespace kvcache {

constexpr int MAX_EVENTS = 64;
constexpr int BUFFER_SIZE = 4096;

TcpServer::TcpServer(int port, int thread_pool_size)
    : port_(port), server_fd_(-1), epoll_fd_(-1), running_(false), thread_pool_(thread_pool_size) {}

TcpServer::~TcpServer() { stop(); }

void TcpServer::setHandler(Handler handler) { handler_ = std::move(handler); }

void TcpServer::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return;
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void TcpServer::start() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        throw std::runtime_error("Failed to create socket");
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
        throw std::runtime_error("Failed to bind socket");
    }

    if (listen(server_fd_, SOMAXCONN) < 0) {
        throw std::runtime_error("Failed to listen");
    }

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        throw std::runtime_error("Failed to create epoll");
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = server_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &event) < 0) {
        throw std::runtime_error("Failed to add server socket to epoll");
    }

    running_ = true;
    std::cout << "Server started on port " << port_ << std::endl;
    eventLoop();
}

void TcpServer::stop() {
    running_ = false;
    if (server_fd_ != -1) {
        close(server_fd_);
        server_fd_ = -1;
    }
    if (epoll_fd_ != -1) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

void TcpServer::eventLoop() {
    std::vector<epoll_event> events(MAX_EVENTS);

    while (running_) {
        int n = epoll_wait(epoll_fd_, events.data(), MAX_EVENTS, 1000);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == server_fd_) {
                handleNewConnection();
            } else {
                int client_fd = events[i].data.fd;
                handleClientData(client_fd);
            }
        }
    }
}

void TcpServer::handleNewConnection() {
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);

    if (client_fd < 0) return;

    setNonBlocking(client_fd);

    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto conn = std::make_shared<Connection>();
        conn->fd = client_fd;
        connections_[client_fd] = conn;
    }

    epoll_event event{};
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    event.data.fd = client_fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event);
}

void TcpServer::removeConnection(int fd) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.erase(fd);
    close(fd);
}

void TcpServer::handleClientData(int client_fd) {
    thread_pool_.enqueue([this, client_fd]() {
        std::shared_ptr<Connection> conn;
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            auto it = connections_.find(client_fd);
            if (it == connections_.end()) return;  // Already removed
            conn = it->second;
        }

        std::lock_guard<std::mutex> conn_lock(conn->mutex);

        std::array<char, BUFFER_SIZE> buffer;
        bool closed = false;

        // Read all available data (Edge Triggered)
        while (true) {
            ssize_t count = read(client_fd, buffer.data(), BUFFER_SIZE);
            if (count < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                closed = true;
                break;
            }
            if (count == 0) {
                closed = true;
                break;
            }
            conn->read_buffer.insert(conn->read_buffer.end(), buffer.begin(), buffer.begin() + count);
        }

        if (closed) {
            removeConnection(client_fd);
            return;
        }

        // Process buffer
        if (handler_) {
            while (!conn->read_buffer.empty()) {
                size_t consumed = 0;
                auto response = handler_(conn->read_buffer, consumed);

                if (consumed > 0) {
                    // Remove consumed bytes
                    conn->read_buffer.erase(conn->read_buffer.begin(), conn->read_buffer.begin() + consumed);

                    if (!response.empty()) {
                        // Write response
                        // TODO: Handle partial writes properly
                        send(client_fd, response.data(), response.size(), 0);
                    }
                } else {
                    // Not enough data
                    break;
                }
            }
        }

        // Re-arm EPOLLONESHOT
        epoll_event event{};
        event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
        event.data.fd = client_fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd, &event);
    });
}

}  // namespace kvcache
