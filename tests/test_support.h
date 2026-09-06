#pragma once
#include "kvrpc/error.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <chrono>
#include <functional>
#include <future>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string("Check failed: ") + #condition + " at " + __FILE__ + ":" + std::to_string(__LINE__)); } while (false)

template<class E = std::exception, class F> void Throws(F&& f) {
    bool caught = false;
    try { f(); } catch (const E&) { caught = true; }
    CHECK(caught);
}
inline const char* ErrorName(kvrpc::ErrorCode code) {
    switch (code) {
        case kvrpc::ErrorCode::invalid_argument: return "invalid_argument";
        case kvrpc::ErrorCode::overloaded: return "overloaded";
        case kvrpc::ErrorCode::closed: return "closed";
        case kvrpc::ErrorCode::timeout: return "timeout";
        case kvrpc::ErrorCode::connection: return "connection";
        case kvrpc::ErrorCode::transport: return "transport";
        case kvrpc::ErrorCode::remote: return "remote";
        case kvrpc::ErrorCode::protocol: return "protocol";
    }
    return "unknown";
}
template<class F> void ErrorIsOneOf(std::initializer_list<kvrpc::ErrorCode> codes, F&& f) {
    std::string expected;
    for (auto code : codes) {
        if (!expected.empty()) expected += " or ";
        expected += ErrorName(code);
    }
    try { f(); } catch (const kvrpc::Error& e) {
        for (auto code : codes) if (e.code() == code) return;
        throw std::runtime_error("Expected error " + expected + ", received " + ErrorName(e.code()) + ": " + e.what());
    }
    throw std::runtime_error("Expected error " + expected + ", but no exception was thrown");
}
template<class F> void ErrorIs(kvrpc::ErrorCode code, F&& f) {
    ErrorIsOneOf({code}, std::forward<F>(f));
}
inline void Read(int fd, char* data, size_t size) {
    while (size) {
        auto n = recv(fd, data, size, 0);
        if (n < 0 && errno == EINTR) continue;
        CHECK(n > 0); data += n; size -= static_cast<size_t>(n);
    }
}
inline void Write(int fd, const char* data, size_t size, bool fragment = false) {
    while (size) {
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;
#endif
        auto n = send(fd, data, fragment ? 1 : size, flags);
        if (n < 0 && errno == EINTR) continue;
        CHECK(n > 0); data += n; size -= static_cast<size_t>(n);
    }
}
class TestServer {
public:
    explicit TestServer(std::function<void(int)> handler, size_t connections = 1) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0); CHECK(fd_ >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        CHECK(bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        socklen_t len = sizeof(address);
        CHECK(getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &len) == 0);
        port_ = ntohs(address.sin_port);
        CHECK(listen(fd_, 32) == 0);
        result_ = std::async(std::launch::async, [this, handler, connections] {
            for (size_t i = 0; i < connections; ++i) {
                pollfd p{fd_, POLLIN, 0}; CHECK(poll(&p, 1, 5000) > 0);
                int client = accept(fd_, nullptr, nullptr); CHECK(client >= 0);
                timeval timeout{2, 0};
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
                int one = 1; setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
                try { handler(client); } catch (...) { close(client); throw; }
                close(client);
            }
        });
    }
    ~TestServer() {
        shutdown(fd_, SHUT_RDWR);
        if (result_.valid()) result_.wait();
        close(fd_);
    }
    uint16_t port() const { return port_; }
    void Finish() { result_.get(); }
private:
    int fd_ = -1;
    uint16_t port_ = 0;
    std::future<void> result_;
};
