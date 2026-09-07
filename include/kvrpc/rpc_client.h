#pragma once
#include <atomic>

#include "kvrpc/async_transport.h"
#include "kvrpc/connection_pool.h"
#include "kvrpc/rpc_protocol.h"
namespace kvrpc {
class RpcClient {
   public:
    explicit RpcClient(ClientEndpoint endpoint, ClientOptions options = {}) : options_(options) {
        transport_ = std::make_unique<AsyncTransport>(endpoint.address, endpoint.port, endpoint.connections,
                                                      endpoint.timeouts, options, RpcFrameSize, RpcId);
    }
    explicit RpcClient(std::shared_ptr<ConnectionPool> pool, ClientOptions options = {})
        : RpcClient(Endpoint(pool), options) {}
    template <class Ret, class... Args>
    std::future<Ret> Call(const std::string& name, const Args&... args) {
        wire::Envelope request;
        request.set_method(name);
        (ToValue(*request.add_arguments(), args), ...);
        auto id = next_.fetch_add(1);
        auto bytes = RpcEncode(id, request, options_.max_frame_bytes);
        auto promise = std::make_shared<std::promise<Ret>>();
        auto result = promise->get_future();
        transport_->Submit(id, std::move(bytes), [promise](auto bytes, std::exception_ptr error) -> bool {
            try {
                if (error) {
                    promise->set_exception(std::move(error));
                    return true;
                }
                auto response = RpcDecode(bytes);
                if (response.type() != wire::Envelope::RESPONSE || response.status() > 4)
                    throw Error(ErrorCode::protocol, "Invalid RPC response type or status");
                if (response.status()) throw RemoteError(response.status(), response.error());
                if constexpr (std::is_void_v<Ret>) {
                    if (response.has_result()) throw Error(ErrorCode::protocol, "Unexpected result");
                    promise->set_value();
                } else
                    promise->set_value(FromValue<Ret>(response.result()));
                return true;
            } catch (const Error& failure) {
                bool valid = failure.code() != ErrorCode::protocol;
                promise->set_exception(std::current_exception());
                return valid;
            } catch (...) {
                promise->set_exception(std::current_exception());
                return false;
            }
        });
        return result;
    }

   private:
    static ClientEndpoint Endpoint(const std::shared_ptr<ConnectionPool>& pool) {
        if (!pool) throw Error(ErrorCode::invalid_argument, "Connection endpoint required");
        return pool->Endpoint();
    }
    ClientOptions options_;
    std::atomic<uint64_t> next_{1};
    std::unique_ptr<AsyncTransport> transport_;
};
}  // namespace kvrpc
