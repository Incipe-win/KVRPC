#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>

namespace kvrpc {

class TcpConnection {
   private:
    int fd_;
    std::string ip_;
    uint16_t port_;

   public:
    TcpConnection() : fd_(-1), port_(0) {}
    ~TcpConnection() { Close(); }

    bool Connect(const std::string& ip, uint16_t port);
    bool SendAll(const char* data, size_t len);
    bool RecvAll(char* buffer, size_t len);
    void Close();

    bool IsConnected() const { return fd_ != -1; }
};

}  // namespace kvrpc
