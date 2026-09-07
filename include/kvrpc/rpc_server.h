#pragma once
#include <tuple>
#include <unordered_map>

#include "kvrpc/rpc_protocol.h"
#include "kvrpc/tcp_server.h"
namespace kvrpc {
// Register before Start. Registered callbacks must support concurrent execution.
class RpcServer {
   public:
    explicit RpcServer(int port, std::string address = "127.0.0.1", size_t peers = 4096,
                       size_t max_payload = 2 * 1024 * 1024)
        : server_(port, std::move(address), peers, max_payload + RPC_HEADER), limit_(max_payload) {
        server_.setFrameSizer(RpcFrameSize);
        server_.setHandler([this](const auto& bytes, size_t& consumed) { return Dispatch(bytes, consumed); });
    }
    template <class Ret, class... Args, class F>
    void Register(const std::string& name, F function) {
        if (name.empty() || name.size() > limit_) throw std::invalid_argument("Invalid method name");
        auto invoke = [function = std::move(function)](const wire::Envelope& request, wire::Envelope& result) mutable {
            std::tuple<std::decay_t<Args>...> args;
            if (request.arguments_size() != sizeof...(Args)) throw BadArguments{};
            try {
                Decode(args, request, std::index_sequence_for<Args...>{});
            } catch (...) {
                throw BadArguments{};
            }
            if constexpr (std::is_void_v<Ret>)
                std::apply(function, args);
            else
                ToValue(*result.mutable_result(), static_cast<Ret>(std::apply(function, args)));
        };
        if (!methods_.emplace(name, std::move(invoke)).second) throw std::invalid_argument("Duplicate RPC method");
    }
    void Configure(ServerOptions options) { server_.configure(options); }
    auto Stats() const { return server_.stats(); }
    uint16_t Port() const noexcept { return server_.port(); }
    void Start() { server_.start(); }
    void Stop() noexcept { server_.stop(); }

   private:
    struct BadArguments {};
    template <class Tuple, size_t... I>
    static void Decode(Tuple& args, const wire::Envelope& request, std::index_sequence<I...>) {
        ((std::get<I>(args) = FromValue<std::tuple_element_t<I, Tuple>>(request.arguments(I))), ...);
    }
    std::vector<uint8_t> Dispatch(const std::vector<uint8_t>& bytes, size_t& consumed) {
        consumed = bytes.size();
        auto request = RpcDecode(bytes);
        auto id = RpcId(bytes);
        if (request.type() != wire::Envelope::REQUEST) throw std::runtime_error("Expected RPC request");
        wire::Envelope result;
        result.set_type(wire::Envelope::RESPONSE);
        auto method = methods_.find(request.method());
        if (method == methods_.end()) {
            result.set_status(1);
            result.set_error("Method not found");
        } else {
            try {
                method->second(request, result);
            } catch (const BadArguments&) {
                result.clear_result();
                result.set_status(2);
                result.set_error("Invalid method arguments");
            } catch (...) {
                result.clear_result();
                result.set_status(3);
                result.set_error("Method execution failed");
            }
        }
        return RpcEncode(id, result, limit_);
    }
    TcpServer server_;
    size_t limit_;
    std::unordered_map<std::string, std::function<void(const wire::Envelope&, wire::Envelope&)>> methods_;
};
}  // namespace kvrpc
