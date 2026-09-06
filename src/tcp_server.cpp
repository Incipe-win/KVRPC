#include "kvrpc/tcp_server.h"
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

namespace kvrpc {
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
    bool buffered_work = false;
    bool dead = false;
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
TcpServer::TcpServer(int port, std::string address, size_t max_connections, size_t max_frame_bytes)
    : port_(port), bind_address_(std::move(address)), max_connections_(max_connections), max_frame_bytes_(max_frame_bytes) {
    in_addr parsed{};
    if (!max_frame_bytes || max_frame_bytes > 64 * 1024 * 1024 + 4 || port < 0 || port > 65535 || !max_connections || max_connections > 4096 ||
        inet_pton(AF_INET, bind_address_.c_str(), &parsed) != 1)
        throw std::invalid_argument("Invalid server address, port, or connection limit");
}
void TcpServer::start() {
    if (!handler_ && !batch_handler_) throw std::logic_error("A request handler is required");
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
    socklen_t address_size = sizeof(address);
    if (getsockname(listener.fd, reinterpret_cast<sockaddr*>(&address), &address_size) < 0)
        throw std::runtime_error("Cannot inspect listener");
    bound_port_ = ntohs(address.sin_port);
    std::vector<std::unique_ptr<Peer>> peers;
    auto last_error_log = Clock::now() - std::chrono::seconds(1);
    std::cout << "Listening on " << bind_address_ << ':' << port_ << std::endl;
    while (!stopping_) {
        std::vector<pollfd> events{{listener.fd, POLLIN, 0}};
        for (const auto& peer : peers)
            events.push_back({peer->fd, static_cast<short>(peer->output.empty() ? POLLIN : POLLOUT), 0});
        bool buffered = false;
        for (const auto& peer : peers) buffered |= peer->buffered_work && peer->output.empty();
        int count = poll(events.data(), events.size(), buffered ? 0 : 100);
        if (count < 0) { if (errno == EINTR) continue; throw std::runtime_error("Server poll failed"); }
        std::vector<Input> inputs;
        std::vector<Peer*> ready;
        for (size_t i = 0; i < peers.size(); ++i) {
            auto& peer = *peers[i];
            auto revents = events[i + 1].revents;
            peer.dead = Clock::now() >= peer.deadline || (revents & (POLLERR | POLLNVAL));
            bool process = peer.buffered_work;
            peer.buffered_work = false;
            if (!peer.dead && peer.output.empty() && (revents & (POLLIN | POLLHUP))) {
                std::array<uint8_t, 16384> bytes;
                auto n = recv(peer.fd, bytes.data(), std::min(bytes.size(), max_frame_bytes_ - peer.input.size()), 0);
                if (n > 0) { peer.input.insert(peer.input.end(), bytes.begin(), bytes.begin() + n); process = true; }
                else if (n == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) peer.dead = true;
            }
            if (!peer.dead && peer.output.empty() && !peer.input.empty() && process) {
                inputs.emplace_back(std::cref(peer.input));
                ready.push_back(&peer);
            }
        }
        if (!inputs.empty()) {
            std::vector<Reply> replies;
            try {
                if (batch_handler_) replies = batch_handler_(inputs);
                else {
                    for (const auto& input : inputs) {
                        Reply reply;
                        try { reply.bytes = handler_(input.get(), reply.consumed); }
                        catch (...) { reply.close = true; }
                        replies.push_back(std::move(reply));
                    }
                }
                if (replies.size() != inputs.size()) throw std::runtime_error("Invalid batch reply count");
            } catch (...) {
                replies.assign(inputs.size(), Reply{0, {}, true});
            }
            for (size_t i = 0; i < replies.size(); ++i) {
                auto& peer = *ready[i];
                auto& reply = replies[i];
                if (reply.close || reply.consumed > peer.input.size() || reply.bytes.size() > max_frame_bytes_ ||
                    (!reply.consumed && (peer.input.size() == max_frame_bytes_ || !reply.bytes.empty()))) {
                    peer.dead = true;
                    if (Clock::now() - last_error_log >= std::chrono::seconds(1)) {
                        std::cerr << "Request rejected: invalid frame or handler failure\n";
                        last_error_log = Clock::now();
                    }
                } else if (reply.consumed) {
                    peer.input.erase(peer.input.begin(), peer.input.begin() + reply.consumed);
                    peer.output = std::move(reply.bytes);
                    peer.sent = 0;
                    peer.buffered_work = !peer.input.empty();
                }
            }
        }
        // A batch handler finishes its durability barrier before ANY reply can be sent.
        for (size_t i = peers.size(); i > 0; --i) {
            auto& peer = *peers[i - 1];
            if (!peer.dead && !peer.output.empty()) {
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
                        peer.buffered_work = !peer.input.empty();
                    }
                } else if (n == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) peer.dead = true;
            }
            if (peer.dead) peers.erase(peers.begin() + static_cast<std::ptrdiff_t>(i - 1));
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
}  // namespace kvrpc
