#include "kvrpc/tcp_connection.h"
#include <iostream>

namespace kvrpc {

bool TcpConnection::Connect(const std::string& ip, uint16_t port) {
    if (fd_ != -1) {
        Close();
    }

    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        return false;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
        Close();
        return false;
    }

    if (connect(fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        Close();
        return false;
    }

    ip_ = ip;
    port_ = port;
    return true;
}

bool TcpConnection::SendAll(const char* data, size_t len) {
    if (fd_ == -1) return false;
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = send(fd_, data + total_sent, len - total_sent, 0);
        if (sent <= 0) { // Error or connection closed
            Close();
            return false;
        }
        total_sent += sent;
    }
    return true;
}

bool TcpConnection::RecvAll(char* buffer, size_t len) {
    if (fd_ == -1) return false;
    size_t total_recv = 0;
    while (total_recv < len) {
        ssize_t r = recv(fd_, buffer + total_recv, len - total_recv, 0);
        if (r <= 0) { // Error or connection closed
            Close();
            return false;
        }
        total_recv += r;
    }
    return true;
}

void TcpConnection::Close() {
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
    }
}

} // namespace kvrpc
