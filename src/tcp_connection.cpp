#include "kvrpc/tcp_connection.h"
#include "kvrpc/error.h"
#include <arpa/inet.h>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>

namespace kvrpc {
TcpConnection::TcpConnection(TransportOptions options) : options_(options) {
    if (options.connect_timeout.count() <= 0 || options.io_timeout.count() <= 0 ||
        options.connect_timeout.count() > INT_MAX || options.io_timeout.count() > INT_MAX)
        throw Error(ErrorCode::invalid_argument, "Transport timeouts must be in [1, INT_MAX] milliseconds");
    BeginRequest();
}
void TcpConnection::BeginRequest() {
    timed_out_ = false;
    deadline_ = std::chrono::steady_clock::now() + options_.io_timeout;
}
bool TcpConnection::Wait(short events, std::chrono::steady_clock::time_point deadline) {
    for (;;) {
        auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= decltype(remaining)::zero()) { timed_out_ = true; return false; }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count() + 1;
        pollfd pfd{fd_, events, 0};
        int result = poll(&pfd, 1, static_cast<int>(std::min<long long>(ms, INT_MAX)));
        if (result > 0) return !(pfd.revents & POLLNVAL); // recv/send diagnoses EOF and errors.
        if (result == 0) { timed_out_ = true; return false; }
        if (errno != EINTR) return false;
    }
}
bool TcpConnection::Connect(const std::string& ip, uint16_t port) {
    Close();
    timed_out_ = false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (!port || inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1) return false;
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) return false;
    const int flags = fcntl(fd_, F_GETFL, 0);
    int one = 1;
    if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0 ||
        fcntl(fd_, F_SETFD, FD_CLOEXEC) < 0 ||
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
        Close(); return false;
    }
#ifdef SO_NOSIGPIPE
    if (setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) < 0) { Close(); return false; }
#endif
    int result = connect(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (result < 0) {
        if ((errno != EINPROGRESS && errno != EINTR) ||
            !Wait(POLLOUT, std::chrono::steady_clock::now() + options_.connect_timeout)) {
            Close(); return false;
        }
        int error = 0;
        socklen_t size = sizeof(error);
        if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &size) < 0 || error) { Close(); return false; }
    }
    BeginRequest();
    return true;
}
bool TcpConnection::SendAll(const char* data, size_t len) {
    if (fd_ < 0 || (!data && len)) return false;
    size_t offset = 0;
    while (offset < len) {
        if (!Wait(POLLOUT, deadline_)) { Close(); return false; }
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        auto n = send(fd_, data + offset, std::min<size_t>(len - offset, INT_MAX), flags);
        if (n > 0) { offset += static_cast<size_t>(n); continue; }
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        Close(); return false;
    }
    return true;
}
bool TcpConnection::RecvAll(char* data, size_t len) {
    if (fd_ < 0 || (!data && len)) return false;
    size_t offset = 0;
    while (offset < len) {
        if (!Wait(POLLIN, deadline_)) { Close(); return false; }
        auto n = recv(fd_, data + offset, std::min<size_t>(len - offset, INT_MAX), 0);
        if (n > 0) { offset += static_cast<size_t>(n); continue; }
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        Close(); return false;
    }
    return true;
}
void TcpConnection::Close() noexcept {
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
}
}  // namespace kvrpc
