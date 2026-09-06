#include "tcp_server.h"
#include "protocol.h"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <memory>
#include <stdexcept>

namespace kvcache {
namespace {
using Clock = std::chrono::steady_clock;
struct Socket {
    int fd;
    explicit Socket(int value) : fd(value) {}
    ~Socket() { if (fd >= 0) close(fd); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
};
struct Peer : Socket {
    explicit Peer(int value) : Socket(value) {}
    std::vector<uint8_t> input, output;
    size_t sent = 0;
    Clock::time_point deadline = Clock::now() + std::chrono::seconds(30);
};
void NonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
        throw std::runtime_error("Cannot configure socket");
#ifdef SO_NOSIGPIPE
    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) < 0)
        throw std::runtime_error("Cannot suppress SIGPIPE");
#endif
}
}
TcpServer::TcpServer(int port, std::string address, size_t max_connections)
    : port_(port), bind_address_(std::move(address)), max_connections_(max_connections) {
    in_addr parsed{};
    if (port < 1 || port > 65535 || !max_connections || max_connections > 4096 ||
        inet_pton(AF_INET, bind_address_.c_str(), &parsed) != 1)
        throw std::invalid_argument("Invalid server address, port, or connection limit");
}
void TcpServer::start() {
    if (!handler_) throw std::logic_error("A request handler is required");
    Socket listener(socket(AF_INET, SOCK_STREAM, 0));
    if (listener.fd < 0) throw std::runtime_error("Cannot create listener");
    NonBlocking(listener.fd);
    int one = 1;
    if (setsockopt(listener.fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
        throw std::runtime_error("Cannot configure listener");
    sockaddr_in address{};
    address.sin_family = AF_INET; address.sin_port = htons(static_cast<uint16_t>(port_));
    inet_pton(AF_INET, bind_address_.c_str(), &address.sin_addr);
    if (bind(listener.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(listener.fd, 128) < 0)
        throw std::runtime_error("Cannot bind or listen");
    std::vector<std::unique_ptr<Peer>> peers;
    auto last_error_log = Clock::now() - std::chrono::seconds(1);
    std::cout << "Listening on " << bind_address_ << ':' << port_ << std::endl;
    while (!stopping_) {
        std::vector<pollfd> events{{listener.fd, POLLIN, 0}};
        for (const auto& peer : peers)
            events.push_back({peer->fd, static_cast<short>(peer->output.empty() ? POLLIN : POLLOUT), 0});
        int count = poll(events.data(), events.size(), 100);
        if (count < 0) { if (errno == EINTR) continue; throw std::runtime_error("Server poll failed"); }
        // Reverse traversal allows erasure without invalidating the event-to-peer mapping.
        for (size_t i = peers.size(); i > 0; --i) {
            auto& peer = *peers[i - 1];
            auto revents = events[i].revents;
            bool dead = Clock::now() >= peer.deadline || (revents & (POLLERR | POLLNVAL));
            try {
                if (!dead && peer.output.empty() && (revents & (POLLIN | POLLHUP))) {
                    std::array<uint8_t, 16384> bytes;
                    // One bounded read per turn preserves fairness across clients.
                    auto n = recv(peer.fd, bytes.data(), std::min(bytes.size(), MAX_FRAME_SIZE - peer.input.size()), 0);
                    if (n > 0) peer.input.insert(peer.input.end(), bytes.begin(), bytes.begin() + n);
                    else if (n == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) dead = true;
                }
                if (!dead && peer.output.empty() && !peer.input.empty()) {
                    size_t consumed = 0;
                    auto response = handler_(peer.input, consumed);
                    if (consumed > peer.input.size() || response.size() > MAX_FRAME_SIZE ||
                        (!consumed && peer.input.size() == MAX_FRAME_SIZE))
                        throw std::runtime_error("Invalid handler frame size");
                    if (consumed) {
                        peer.input.erase(peer.input.begin(), peer.input.begin() + consumed);
                        peer.output = std::move(response); peer.sent = 0;
                    }
                }
                if (!dead && !peer.output.empty()) {
#ifdef MSG_NOSIGNAL
                    constexpr int flags = MSG_NOSIGNAL;
#else
                    constexpr int flags = 0;
#endif
                    auto n = send(peer.fd, peer.output.data() + peer.sent, peer.output.size() - peer.sent, flags);
                    if (n > 0) {
                        peer.sent += static_cast<size_t>(n);
                        if (peer.sent == peer.output.size()) {
                            peer.output.clear(); peer.sent = 0;
                            peer.deadline = Clock::now() + std::chrono::seconds(30);
                        }
                    } else if (n == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) dead = true;
                }
            } catch (const std::exception& e) {
                // No application keys or values are written to logs.
                if (Clock::now() - last_error_log >= std::chrono::seconds(1)) {
                    std::cerr << "Request rejected: " << e.what() << '\n';
                    last_error_log = Clock::now();
                }
                dead = true;
            }
            if (dead) peers.erase(peers.begin() + static_cast<std::ptrdiff_t>(i - 1));
        }
        if (events[0].revents & POLLIN) {
            // Bound accepts per iteration, including rejection under overload.
            for (int i = 0; i < 32; ++i) {
                int fd = accept(listener.fd, nullptr, nullptr);
                if (fd < 0) {
                    if (errno == EINTR) continue;
                    break;
                }
                auto peer = std::make_unique<Peer>(fd);
                NonBlocking(fd);
                if (peers.size() < max_connections_) peers.push_back(std::move(peer));
            }
        }
    }
}
}  // namespace kvcache
