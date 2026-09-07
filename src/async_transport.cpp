#include "kvrpc/async_transport.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <deque>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "kvrpc/buffer.h"
#include "kvrpc/event_loop.h"

namespace kvrpc {
struct AsyncTransport::Impl {
    struct Request {
        uint64_t id, timer = 0;
        Bytes bytes;
        Completion complete;
        size_t slot = SIZE_MAX;
        bool sent = false;
    };
    struct Connection {
        int fd = -1;
        uint64_t token = 0, connect_timer = 0;
        bool connecting = false, scheduled = false;
        Buffer input;
        std::deque<std::shared_ptr<Request>> output;
        std::deque<uint64_t> order;
        size_t sent = 0, inflight = 0;
        std::unordered_set<uint64_t> retired;
    };
    EventLoop loop;
    std::thread thread;
    std::string address;
    uint16_t port;
    TransportOptions transport;
    ClientOptions options;
    FrameSize framing;
    ResponseId ids;
    std::vector<Connection> sockets;
    std::deque<std::shared_ptr<Request>> waiting;
    std::unordered_map<uint64_t, std::shared_ptr<Request>> requests;
    std::mutex admission;
    size_t admitted = 0, bytes = 0;
    bool closing = false, draining = false;
    Impl(std::string a, uint16_t p, size_t n, TransportOptions t, ClientOptions o, FrameSize f, ResponseId i)
        : address(std::move(a)),
          port(p),
          transport(t),
          options(o),
          framing(std::move(f)),
          ids(std::move(i)),
          sockets(n) {
        in_addr parsed{};
        if (!p || inet_pton(AF_INET, address.c_str(), &parsed) != 1 || t.io_timeout.count() <= 0 ||
            t.connect_timeout.count() <= 0 || !n || !o.queue_capacity || !o.max_frame_bytes || !o.queue_bytes ||
            !o.max_inflight)
            throw Error(ErrorCode::invalid_argument, "Invalid asynchronous transport limits");
        thread = std::thread([this] { loop.Run(); });
    }
    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(admission);
            closing = true;
        }
        loop.Post([this] {
            draining = true;
            if (requests.empty()) loop.Stop();
        });
        thread.join();
        for (auto& c : sockets)
            if (c.fd >= 0) close(c.fd);
    }
    void Finish(uint64_t id, Bytes response, std::exception_ptr error) {
        auto it = requests.find(id);
        if (it == requests.end()) return;
        auto request = it->second;
        requests.erase(it);
        loop.Cancel(request->timer);
        if (request->slot != SIZE_MAX)
            --sockets[request->slot].inflight;
        else
            waiting.erase(
                std::remove_if(waiting.begin(), waiting.end(), [id](const auto& item) { return item->id == id; }),
                waiting.end());
        {
            std::lock_guard<std::mutex> lock(admission);
            --admitted;
            bytes -= request->bytes.size();
        }
        bool valid = request->complete(std::move(response), std::move(error));
        if (!valid && request->slot != SIZE_MAX)
            Disconnect(request->slot, ErrorCode::protocol, "Invalid response payload");
        if (draining && requests.empty()) loop.Stop();
    }
    void Disconnect(size_t index, ErrorCode code, const char* message) {
        auto& c = sockets[index];
        if (c.fd >= 0) {
            loop.Remove(c.token);
            close(c.fd);
        }
        loop.Cancel(c.connect_timer);
        c.fd = -1;
        c.input = {};
        c.output.clear();
        c.order.clear();
        c.retired.clear();
        c.sent = 0;
        c.connecting = false;
        std::vector<uint64_t> failed;
        for (const auto& item : requests)
            if (item.second->slot == index) failed.push_back(item.first);
        for (auto id : failed) Finish(id, {}, std::make_exception_ptr(Error(code, message)));
    }
    void Connect(size_t index) {
        auto& c = sockets[index];
        c.fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (c.fd < 0) {
            Disconnect(index, ErrorCode::connection, "Cannot create connection");
            return;
        }
        int one = 1;
        setsockopt(c.fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(port);
        inet_pton(AF_INET, address.c_str(), &target.sin_addr);
        int result = connect(c.fd, reinterpret_cast<sockaddr*>(&target), sizeof(target));
        if (result < 0 && errno != EINPROGRESS && errno != EINTR) {
            Disconnect(index, ErrorCode::connection, "Connection failed");
            return;
        }
        c.connecting = result < 0;
        c.token = loop.Add(c.fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP, [this, index](uint32_t) {
            Pump(index);
            Dispatch();
        });
        if (c.connecting)
            c.connect_timer = loop.At(EventLoop::Clock::now() + transport.connect_timeout, [this, index] {
                Disconnect(index, ErrorCode::timeout, "Connection deadline exceeded");
                Dispatch();
            });
    }
    void Schedule(size_t index) {
        auto& c = sockets[index];
        if (c.scheduled) return;
        c.scheduled = true;
        loop.Post([this, index] {
            sockets[index].scheduled = false;
            Pump(index);
            Dispatch();
        });
    }
    void Pump(size_t index) {
        auto& c = sockets[index];
        if (c.fd < 0) return;
        if (c.connecting) {
            int error = 0;
            socklen_t length = sizeof(error);
            if (getsockopt(c.fd, SOL_SOCKET, SO_ERROR, &error, &length) || error) {
                Disconnect(index, ErrorCode::connection, "Connection failed");
                return;
            }
            sockaddr_in peer{};
            length = sizeof(peer);
            if (getpeername(c.fd, reinterpret_cast<sockaddr*>(&peer), &length) < 0) return;
            c.connecting = false;
            loop.Cancel(c.connect_timer);
        }
        size_t budget = 256 * 1024;
        while (!c.output.empty() && budget) {
            const auto& data = c.output.front()->bytes;
            auto n = send(c.fd, data.data() + c.sent, std::min(budget, data.size() - c.sent), MSG_NOSIGNAL);
            if (n > 0) {
                c.sent += n;
                budget -= n;
                if (c.sent == data.size()) {
                    c.output.front()->sent = true;
                    c.output.pop_front();
                    c.sent = 0;
                }
            } else if (n < 0 && errno == EINTR)
                continue;
            else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            else {
                Disconnect(index, ErrorCode::transport, "Send failed");
                return;
            }
        }
        if (!budget && !c.output.empty()) Schedule(index);
        budget = 256 * 1024;
        while (budget) {
            std::array<uint8_t, 16384> data;
            auto n = recv(c.fd, data.data(), std::min(data.size(), budget), 0);
            if (n > 0) {
                budget -= n;
                c.input.append(data.data(), n);
                try {
                    while (c.input.size()) {
                        auto size = framing(c.input.data(), c.input.size());
                        if (size > options.max_frame_bytes + (ids ? 16 : 0))
                            throw Error(ErrorCode::protocol, "Response frame too large");
                        if (!size || size > c.input.size()) break;
                        auto response = c.input.copy(size);
                        c.input.consume(size);
                        uint64_t id = ids ? ids(response) : (c.order.empty() ? 0 : c.order.front());
                        if (ids && c.retired.erase(id)) continue;
                        auto request = requests.find(id);
                        if (request == requests.end() || request->second->slot != index)
                            throw Error(ErrorCode::protocol, "Unknown response identifier");
                        if (!ids) c.order.pop_front();
                        Finish(id, std::move(response), {});
                        if (c.fd < 0) return;
                    }
                } catch (...) {
                    Disconnect(index, ErrorCode::protocol, "Invalid response frame");
                    return;
                }
            } else if (!n) {
                Disconnect(index, ErrorCode::transport, "Peer disconnected");
                return;
            } else if (errno == EINTR)
                continue;
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            else {
                Disconnect(index, ErrorCode::transport, "Receive failed");
                return;
            }
        }
        if (!budget) Schedule(index);
        loop.Modify(c.token, EPOLLIN | EPOLLRDHUP | (!c.output.empty() ? EPOLLOUT : 0u));
    }
    void Dispatch() {
        for (size_t i = 0; i < sockets.size() && !waiting.empty(); ++i) {
            auto& c = sockets[i];
            while (!waiting.empty() && c.inflight + c.retired.size() < (ids ? options.max_inflight : 1)) {
                auto request = waiting.front();
                waiting.pop_front();
                if (!requests.count(request->id)) continue;
                request->slot = i;
                ++c.inflight;
                c.output.push_back(request);
                if (!ids) c.order.push_back(request->id);
                if (c.fd < 0) Connect(i);
                Pump(i);
            }
        }
    }
};
AsyncTransport::AsyncTransport(std::string a, uint16_t p, size_t n, TransportOptions t, ClientOptions o, FrameSize f,
                               ResponseId ids)
    : impl_(std::make_unique<Impl>(std::move(a), p, n, t, o, std::move(f), std::move(ids))) {}
AsyncTransport::~AsyncTransport() = default;
void AsyncTransport::Submit(uint64_t id, Bytes bytes, Completion complete) {
    auto& p = *impl_;
    auto deadline = EventLoop::Clock::now() + p.transport.io_timeout;
    {
        std::lock_guard<std::mutex> lock(p.admission);
        if (p.closing || p.admitted >= p.options.queue_capacity || bytes.size() > p.options.queue_bytes - p.bytes) {
            complete({}, std::make_exception_ptr(Error(p.closing ? ErrorCode::closed : ErrorCode::overloaded,
                                                       "Client admission limit reached")));
            return;
        }
        ++p.admitted;
        p.bytes += bytes.size();
    }
    auto request = std::make_shared<Impl::Request>();
    request->id = id;
    request->bytes = std::move(bytes);
    request->complete = std::move(complete);
    p.loop.Post([&p, request, deadline] {
        p.requests.emplace(request->id, request);
        request->timer = p.loop.At(deadline, [&p, id = request->id] {
            auto it = p.requests.find(id);
            if (it == p.requests.end()) return;
            if (it->second->slot != SIZE_MAX && p.ids && it->second->sent) {
                p.sockets[it->second->slot].retired.insert(id);
                p.Finish(id, {}, std::make_exception_ptr(Error(ErrorCode::timeout, "Request deadline exceeded")));
            } else if (it->second->slot != SIZE_MAX)
                p.Disconnect(it->second->slot, ErrorCode::timeout, "Request deadline exceeded");
            else
                p.Finish(id, {},
                         std::make_exception_ptr(Error(ErrorCode::timeout, "Request deadline exceeded in queue")));
            p.Dispatch();
        });
        p.waiting.push_back(request);
        p.Dispatch();
    });
}
}  // namespace kvrpc
