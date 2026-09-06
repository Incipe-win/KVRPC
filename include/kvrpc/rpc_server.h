#pragma once
#include "kvrpc/serializer.h"
#include "kvrpc/tcp_server.h"
#include <tuple>
#include <unordered_map>

namespace kvrpc {
// Register before Start(); callbacks execute serially on the server thread.
class RpcServer {
public:
    explicit RpcServer(int port, std::string address = "127.0.0.1", size_t peers = 128,
                       size_t max_payload = 2 * 1024 * 1024)
        : server_(port, std::move(address), peers, CheckedLimit(max_payload) + 4), limit_(max_payload) {
        server_.setHandler([this](const auto& bytes, size_t& consumed) { return Dispatch(bytes, consumed); });
    }
    template<class Ret, class... Args, class F>
    void Register(const std::string& name, F function) {
        if (name.empty() || name.size() + 4 > limit_) throw std::invalid_argument("Invalid method name");
        auto invoke = [function = std::move(function)](Serializer& request, Serializer& result) mutable {
            std::tuple<std::decay_t<Args>...> args;
            try {
                std::apply([&](auto&... value) { request.Deserialize(value...); }, args);
                if (request.Remaining()) throw std::invalid_argument("Trailing arguments");
            } catch (const std::exception&) { throw BadArguments{}; }
            if constexpr (std::is_void_v<Ret>) std::apply(function, args);
            else result.Serialize(static_cast<Ret>(std::apply(function, args)));
        };
        if (!methods_.emplace(name, std::move(invoke)).second)
            throw std::invalid_argument("Duplicate RPC method");
    }
    uint16_t Port() const noexcept { return server_.port(); }
    void Start() { server_.start(); }
    void Stop() noexcept { server_.stop(); }
private:
    struct BadArguments {};
    static size_t CheckedLimit(size_t limit) {
        if (limit < 64 || limit > 64 * 1024 * 1024) throw std::invalid_argument("Invalid RPC frame limit");
        return limit;
    }
    std::vector<uint8_t> Dispatch(const std::vector<uint8_t>& bytes, size_t& consumed) {
        consumed = 0;
        if (bytes.size() < 4) return {};
        Serializer prefix(std::vector<char>(bytes.begin(), bytes.begin() + 4));
        uint32_t size = 0; prefix.Deserialize(size);
        if (size > limit_) throw std::runtime_error("RPC request exceeds frame limit");
        if (bytes.size() < size_t(size) + 4) return {};
        consumed = size_t(size) + 4;
        Serializer request(std::vector<char>(bytes.begin() + 4, bytes.begin() + consumed));
        Serializer result(limit_);
        uint32_t error = 0;
        std::string message;
        std::string name;
        try { request.Deserialize(name); }
        catch (const std::exception&) { error = 2; message = "Invalid method name"; }
        if (!error) {
            auto method = methods_.find(name);
            if (method == methods_.end()) { error = 1; message = "Method not found"; }
            else {
                try { method->second(request, result); }
                catch (const BadArguments&) { error = 2; message = "Invalid method arguments"; }
                catch (...) { error = 3; message = "Method execution failed"; }
            }
        }
        if (error) { result.Reset(); result.Serialize(error, message); }
        Serializer header;
        header.Serialize(static_cast<uint32_t>(result.GetBuffer().size()) | (error ? 0x80000000u : 0u));
        std::vector<uint8_t> response(header.GetBuffer().begin(), header.GetBuffer().end());
        response.insert(response.end(), result.GetBuffer().begin(), result.GetBuffer().end());
        return response;
    }
    TcpServer server_;
    size_t limit_;
    std::unordered_map<std::string, std::function<void(Serializer&, Serializer&)>> methods_;
};
}  // namespace kvrpc
