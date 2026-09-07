#include "kvrpc/tcp_server.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <condition_variable>
#include <deque>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <unordered_map>

#include "kvrpc/buffer.h"
#include "kvrpc/event_loop.h"

namespace kvrpc {
struct TcpServer::Runtime {
    struct Io;
    struct Peer {
        int fd;
        uint64_t token = 0, timer = 0;
        Buffer input;
        std::deque<std::vector<uint8_t>> output;
        size_t sent = 0, output_bytes = 0, pending = 0;
        bool eof = false, closed = false, scheduled = false, incomplete = false;
        explicit Peer(int value) : fd(value) {}
        ~Peer() { close(fd); }
    };
    struct Job {
        std::vector<uint8_t> input;
        std::function<void(Reply)> complete;
    };
    TcpServer& owner;
    EventLoop accept_loop;
    int listener = -1;
    std::vector<std::unique_ptr<Io>> ios;
    std::vector<std::thread> workers;
    std::mutex mutex;
    std::condition_variable available;
    std::deque<Job> jobs;
    size_t queued_bytes = 0;
    bool closing = false;
    std::exception_ptr error;
    explicit Runtime(TcpServer& server) : owner(server) {}
    ~Runtime() {
        if (listener >= 0) close(listener);
    }
    bool Submit(Job job) {
        std::lock_guard<std::mutex> lock(mutex);
        if (closing || jobs.size() >= owner.options_.queue_capacity ||
            job.input.size() > owner.options_.queue_bytes - queued_bytes)
            return false;
        queued_bytes += job.input.size();
        owner.queued_bytes_ += job.input.size();
        ++owner.queued_tasks_;
        jobs.push_back(std::move(job));
        available.notify_one();
        return true;
    }
    void Work() {
        for (;;) {
            std::vector<Job> batch;
            {
                std::unique_lock<std::mutex> lock(mutex);
                available.wait(lock, [&] { return closing || !jobs.empty(); });
                if (jobs.empty()) return;
                size_t count = owner.batch_handler_ ? std::min<size_t>(64, jobs.size()) : 1;
                while (count--) {
                    queued_bytes -= jobs.front().input.size();
                    owner.queued_bytes_ -= jobs.front().input.size();
                    --owner.queued_tasks_;
                    batch.push_back(std::move(jobs.front()));
                    jobs.pop_front();
                }
            }
            std::vector<Reply> replies;
            auto began = EventLoop::Clock::now();
            try {
                if (owner.batch_handler_) {
                    std::vector<Input> inputs;
                    for (auto& job : batch) inputs.emplace_back(std::cref(job.input));
                    replies = owner.batch_handler_(inputs);
                    if (replies.size() != batch.size()) throw std::runtime_error("Invalid batch reply count");
                } else {
                    Reply reply;
                    reply.bytes = owner.handler_(batch[0].input, reply.consumed);
                    replies.push_back(std::move(reply));
                }
            } catch (...) {
                owner.handler_errors_ += batch.size();
                replies.assign(batch.size(), Reply{0, {}, true});
            }
            owner.handler_us_ +=
                std::chrono::duration_cast<std::chrono::microseconds>(EventLoop::Clock::now() - began).count();
            for (size_t i = 0; i < batch.size(); ++i) batch[i].complete(std::move(replies[i]));
        }
    }
    struct Io {
        Runtime& runtime;
        EventLoop loop;
        std::thread thread;
        std::unordered_map<uint64_t, std::shared_ptr<Peer>> peers;
        explicit Io(Runtime& value) : runtime(value) {}
        TcpServer& server() { return runtime.owner; }
        void Close(const std::shared_ptr<Peer>& p) {
            if (p->closed) return;
            p->closed = true;
            loop.Cancel(p->timer);
            loop.Remove(p->token);
            peers.erase(p->token);
            --server().active_;
        }
        void Deadline(const std::shared_ptr<Peer>& p) {
            loop.Cancel(p->timer);
            p->timer = loop.At(EventLoop::Clock::now() + server().options_.idle_timeout,
                               [this, weak = std::weak_ptr<Peer>(p)] {
                                   if (auto peer = weak.lock()) {
                                       ++server().timeouts_;
                                       Close(peer);
                                   }
                               });
        }
        void Schedule(const std::shared_ptr<Peer>& p) {
            if (p->scheduled || p->closed) return;
            p->scheduled = true;
            loop.Post([this, weak = std::weak_ptr<Peer>(p)] {
                if (auto peer = weak.lock()) {
                    peer->scheduled = false;
                    if (!peer->closed) Pump(peer);
                }
            });
        }
        void Attach(int fd) {
            auto p = std::make_shared<Peer>(fd);
            p->token = loop.Add(fd, EPOLLIN | EPOLLRDHUP, [this, weak = std::weak_ptr<Peer>(p)](uint32_t events) {
                if (auto peer = weak.lock()) {
                    if (events & EPOLLERR) {
                        Close(peer);
                        return;
                    }
                    Pump(peer);
                }
            });
            peers.emplace(p->token, p);
            Deadline(p);
        }
        void Pump(const std::shared_ptr<Peer>& p) {
            if (p->closed) return;
            auto& opts = server().options_;
            size_t limit = server().batch_handler_ || !server().sizer_ ? 1 : opts.pipeline;
            // ET work budgets use explicit rescheduling when we stop before EAGAIN.
            size_t budget = 256 * 1024;
            while (!p->output.empty() && budget) {
                auto& bytes = p->output.front();
                auto n = send(p->fd, bytes.data() + p->sent, std::min(budget, bytes.size() - p->sent), MSG_NOSIGNAL);
                if (n > 0) {
                    p->sent += n;
                    p->output_bytes -= n;
                    budget -= n;
                    if (p->sent == bytes.size()) {
                        p->output.pop_front();
                        p->sent = 0;
                        Deadline(p);
                    }
                } else if (n < 0 && errno == EINTR)
                    continue;
                else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    break;
                else {
                    Close(p);
                    return;
                }
            }
            if (!budget && !p->output.empty()) Schedule(p);
            bool can_read = p->pending < limit && p->output_bytes < opts.output_bytes / 2;
            budget = 256 * 1024;
            while (!p->eof && can_read && p->input.size() < server().max_frame_bytes_ && budget) {
                std::array<uint8_t, 16384> bytes;
                auto n = recv(p->fd, bytes.data(),
                              std::min({bytes.size(), budget, server().max_frame_bytes_ - p->input.size()}), 0);
                if (n > 0) {
                    p->input.append(bytes.data(), n);
                    p->incomplete = false;
                    budget -= n;
                } else if (!n) {
                    p->eof = true;
                    break;
                } else if (errno == EINTR)
                    continue;
                else if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                else {
                    Close(p);
                    return;
                }
            }
            if (!budget) Schedule(p);
            size_t frames = 0;
            while (p->input.size() && !p->incomplete && p->pending < limit && p->output_bytes < opts.output_bytes / 2 &&
                   frames++ < 32) {
                size_t size = p->input.size();
                try {
                    if (server().sizer_) size = server().sizer_(p->input.data(), p->input.size());
                    if (size > server().max_frame_bytes_) throw std::runtime_error("Frame exceeds limit");
                } catch (...) {
                    Close(p);
                    return;
                }
                if (!size || size > p->input.size()) break;
                auto input = p->input.copy(size);
                bool framed = bool(server().sizer_);
                if (framed) p->input.consume(size);
                ++p->pending;
                if (!runtime.Submit(
                        {std::move(input), [this, weak = std::weak_ptr<Peer>(p), size, framed](Reply reply) {
                             loop.Post([this, weak, size, framed, reply = std::move(reply)]() mutable {
                                 auto peer = weak.lock();
                                 if (!peer || peer->closed) return;
                                 --peer->pending;
                                 if (reply.close || reply.consumed > size || (framed && reply.consumed != size) ||
                                     reply.bytes.size() > server().max_frame_bytes_ ||
                                     reply.bytes.size() > server().options_.output_bytes - peer->output_bytes ||
                                     (!reply.consumed && (!reply.bytes.empty() || size == server().max_frame_bytes_))) {
                                     Close(peer);
                                     return;
                                 }
                                 if (!framed) {
                                     peer->input.consume(reply.consumed);
                                     peer->incomplete = !reply.consumed;
                                 }
                                 if (reply.consumed) ++server().requests_;
                                 if (!reply.bytes.empty()) {
                                     peer->output_bytes += reply.bytes.size();
                                     peer->output.push_back(std::move(reply.bytes));
                                 }
                                 Pump(peer);
                             });
                         }})) {
                    ++server().rejected_;
                    Close(p);
                    return;
                }
            }
            if (frames >= 32 && p->pending < limit) Schedule(p);
            if (p->eof && !p->pending && p->output.empty()) {
                Close(p);
                return;
            }
            can_read = !p->eof && p->pending < limit && p->output_bytes < opts.output_bytes / 2;
            loop.Modify(p->token, EPOLLRDHUP | (can_read ? EPOLLIN : 0u) | (!p->output.empty() ? EPOLLOUT : 0u));
        }
    };
    void Run() {
        for (size_t i = 0; i < owner.options_.io_threads; ++i) ios.emplace_back(std::make_unique<Io>(*this));
        auto finish = [&] {
            {
                std::lock_guard<std::mutex> lock(mutex);
                closing = true;
            }
            available.notify_all();
            for (auto& thread : workers)
                if (thread.joinable()) thread.join();
            for (auto& io : ios)
                io->loop.Post([p = io.get()] {
                    while (!p->peers.empty()) p->Close(p->peers.begin()->second);
                    p->loop.Stop();
                });
            for (auto& io : ios)
                if (io->thread.joinable()) io->thread.join();
        };
        try {
            size_t threads = owner.batch_handler_ ? 1 : owner.options_.workers;
            for (size_t i = 0; i < threads; ++i) workers.emplace_back([this] { Work(); });
            for (auto& io : ios)
                io->thread = std::thread([this, p = io.get()] {
                    try {
                        p->loop.Run();
                    } catch (...) {
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            error = std::current_exception();
                        }
                        accept_loop.Stop();
                    }
                });
            listener = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            if (listener < 0) throw std::system_error(errno, std::generic_category(), "socket");
            int one = 1;
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(owner.port_);
            inet_pton(AF_INET, owner.bind_address_.c_str(), &address.sin_addr);
            if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(listener, SOMAXCONN))
                throw std::system_error(errno, std::generic_category(), "listen");
            socklen_t length = sizeof(address);
            getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length);
            size_t next = 0;
            std::function<void()> accept_ready;
            accept_ready = [&] {
                for (size_t i = 0; i < 128; ++i) {
                    int fd = accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if (fd < 0) {
                        if (errno == EINTR) {
                            --i;
                            continue;
                        }
                        return;
                    }
                    if (owner.active_ >= owner.max_connections_) {
                        ++owner.rejected_;
                        close(fd);
                        continue;
                    }
                    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                    ++owner.active_;
                    ++owner.accepted_;
                    auto* io = ios[next++ % ios.size()].get();
                    io->loop.Post([io, fd] { io->Attach(fd); });
                }
                accept_loop.Post(accept_ready);
            };
            accept_loop.Add(listener, EPOLLIN, [&](uint32_t) { accept_ready(); });
            owner.bound_port_ = ntohs(address.sin_port);
            if (owner.stopping_) accept_loop.Stop();
            accept_loop.Run();
        } catch (...) {
            finish();
            throw;
        }
        finish();
        if (error) std::rethrow_exception(error);
    }
};
TcpServer::TcpServer(int port, std::string address, size_t connections, size_t bytes)
    : port_(port), bind_address_(std::move(address)), max_connections_(connections), max_frame_bytes_(bytes) {
    in_addr parsed{};
    if (port < 0 || port > 65535 || !connections || !bytes || bytes > 64 * 1024 * 1024 + 16 ||
        inet_pton(AF_INET, bind_address_.c_str(), &parsed) != 1)
        throw std::invalid_argument("Invalid server options");
    options_.output_bytes = std::max(options_.output_bytes, bytes * 2);
}
TcpServer::~TcpServer() = default;
void TcpServer::configure(ServerOptions options) {
    if (!options.io_threads || options.io_threads > 256 || !options.workers || options.workers > 256 ||
        !options.queue_capacity || !options.queue_bytes || options.output_bytes < max_frame_bytes_ ||
        !options.pipeline || options.idle_timeout.count() <= 0)
        throw std::invalid_argument("Invalid server resource options");
    options_ = options;
}
void TcpServer::start() {
    if (!handler_ && !batch_handler_) throw std::logic_error("Handler required");
    auto runtime = std::make_shared<Runtime>(*this);
    {
        std::lock_guard<std::mutex> lock(runtime_mutex_);
        if (runtime_) throw std::logic_error("Server already started");
        runtime_ = runtime;
    }
    runtime->Run();
}
void TcpServer::stop() noexcept {
    stopping_ = true;
    std::lock_guard<std::mutex> lock(runtime_mutex_);
    if (runtime_) runtime_->accept_loop.Stop();
}
TcpServer::Stats TcpServer::stats() const {
    return {accepted_,     active_,       rejected_,   requests_,      timeouts_,
            queued_tasks_, queued_bytes_, handler_us_, handler_errors_};
}
}  // namespace kvrpc
