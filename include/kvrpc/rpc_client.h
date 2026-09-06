#pragma once
#include "kvrpc/connection_pool.h"
#include "kvrpc/executor.h"
#include "kvrpc/serializer.h"
#include <array>

namespace kvrpc {
class RpcClient {
public:
    explicit RpcClient(std::shared_ptr<ConnectionPool> pool, ClientOptions options = {})
        : pool_(std::move(pool)), options_(options), executor_(options) {
        if (!pool_) throw Error(ErrorCode::invalid_argument, "A connection pool is required");
    }
    template<class RetType, class... Args>
    std::future<RetType> Call(const std::string& name, Args... args) {
        // Serialize now so pointer arguments cannot dangle while waiting in the queue.
        Serializer request(options_.max_frame_bytes);
        try { request.Serialize(name, args...); }
        catch (const std::length_error&) { throw Error(ErrorCode::invalid_argument, "RPC request exceeds frame limit"); }
        if (request.GetBuffer().size() > options_.max_frame_bytes)
            throw Error(ErrorCode::invalid_argument, "RPC request exceeds frame limit");
        return executor_.Submit([pool = pool_, limit = options_.max_frame_bytes,
                                 payload = request.GetBuffer()]() -> RetType {
            auto conn = pool->Acquire();
            try {
                Serializer prefix;
                prefix.Serialize(static_cast<uint32_t>(payload.size()));
                auto check = [&](bool ok) {
                    if (!ok) throw Error(conn->TimedOut() ? ErrorCode::timeout : ErrorCode::transport, "RPC transport failed");
                };
                check(conn->SendAll(prefix.GetBuffer().data(), 4));
                check(conn->SendAll(payload.data(), payload.size()));
                std::vector<char> header(4);
                check(conn->RecvAll(header.data(), header.size()));
                Serializer decoded(std::move(header));
                uint32_t size = 0; decoded.Deserialize(size);
                const bool remote_error = (size & 0x80000000u) != 0;
                size &= 0x7fffffffu;
                if (size > limit) throw Error(ErrorCode::protocol, "RPC response exceeds frame limit");
                std::vector<char> response(size);
                check(conn->RecvAll(response.data(), response.size()));
                Serializer result(std::move(response));
                if (remote_error) {
                    uint32_t status = 0;
                    std::string message;
                    try { result.Deserialize(status, message); }
                    catch (const std::exception&) { throw Error(ErrorCode::protocol, "Invalid remote error envelope"); }
                    if (status < 1 || status > 3 || result.Remaining())
                        throw Error(ErrorCode::protocol, "Invalid remote error status");
                    throw RemoteError(status, message);
                }
                if constexpr (std::is_void_v<RetType>) {
                    if (result.Remaining()) throw Error(ErrorCode::protocol, "Unexpected void response payload");
                    return;
                } else {
                    RetType value{};
                    try { result.Deserialize(value); }
                    catch (const std::exception&) { throw Error(ErrorCode::protocol, "Invalid RPC response value"); }
                    if (result.Remaining()) throw Error(ErrorCode::protocol, "Trailing RPC response bytes");
                    return value;
                }
            } catch (const RemoteError&) { throw; }
            catch (...) { conn->Close(); throw; }
        });
    }
private:
    std::shared_ptr<ConnectionPool> pool_;
    ClientOptions options_;
    Executor executor_;
};
}  // namespace kvrpc
