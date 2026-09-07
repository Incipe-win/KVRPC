#pragma once
#include <functional>
#include <memory>
#include <vector>

#include "kvrpc/executor.h"
#include "kvrpc/tcp_connection.h"
namespace kvrpc {
// One ET loop drives a bounded set of persistent connections and all request deadlines.
class AsyncTransport {
   public:
    using Bytes = std::vector<uint8_t>;
    // Return false when a decoded response violates the application protocol.
    using Completion = std::function<bool(Bytes, std::exception_ptr)>;
    using FrameSize = std::function<size_t(const uint8_t*, size_t)>;
    using ResponseId = std::function<uint64_t(const Bytes&)>;
    AsyncTransport(std::string address, uint16_t port, size_t connections, TransportOptions transport,
                   ClientOptions options, FrameSize framing, ResponseId ids = {});
    ~AsyncTransport();
    AsyncTransport(const AsyncTransport&) = delete;
    AsyncTransport& operator=(const AsyncTransport&) = delete;
    void Submit(uint64_t id, Bytes bytes, Completion completion);

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace kvrpc
