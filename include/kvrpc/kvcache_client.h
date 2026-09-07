#pragma once
#include <atomic>
#include <optional>

#include "kvrpc/async_transport.h"
#include "kvrpc/connection_pool.h"
#include "kvrpc/kv_protocol.h"
#include "rpc.pb.h"

namespace kvrpc {
class KVCacheClient {
   public:
    explicit KVCacheClient(ClientEndpoint endpoint, ClientOptions options = {}) : options_(options) {
        transport_ =
            std::make_unique<AsyncTransport>(endpoint.address, endpoint.port, endpoint.connections, endpoint.timeouts,
                                             options, [](const uint8_t* bytes, size_t size) -> size_t {
                                                 if (size < kvcache::HEADER_SIZE) return 0;
                                                 auto h = kvcache::Message::decodeHeader(bytes);
                                                 kvcache::Message::validate(h);
                                                 return kvcache::HEADER_SIZE + size_t(h.key_len) + h.value_len;
                                             });
    }
    explicit KVCacheClient(std::shared_ptr<ConnectionPool> pool, ClientOptions options = {})
        : KVCacheClient(Endpoint(pool), options) {}
    std::future<bool> Set(const std::string& key, const std::string& value) {
        return Request<bool>(kvcache::Command::SET, key, value);
    }
    // Version 1 represents both missing keys and empty values as an empty string.
    std::future<std::string> Get(const std::string& key) { return Request<std::string>(kvcache::Command::GET, key); }
    std::future<bool> Delete(const std::string& key) { return Request<bool>(kvcache::Command::DEL, key); }
    std::future<std::string> Stats() { return Request<std::string>(kvcache::Command::STATS, ""); }
    std::future<std::optional<std::string>> Lookup(const std::string& key) {
        return Request<std::optional<std::string>>(kvcache::Command::LOOKUP, key);
    }
    std::future<bool> SetWithTTL(const std::string& key, const std::string& value, std::chrono::milliseconds ttl) {
        if (ttl.count() <= 0 || value.size() > kvcache::MAX_VALUE_SIZE)
            throw Error(ErrorCode::invalid_argument, "TTL must be positive");
        wire::CacheValue item;
        item.set_value(value);
        item.set_ttl_ms(ttl.count());
        return Request<bool>(kvcache::Command::SET_TTL, key, item.SerializeAsString());
    }

   private:
    static ClientEndpoint Endpoint(const std::shared_ptr<ConnectionPool>& pool) {
        if (!pool) throw Error(ErrorCode::invalid_argument, "Connection endpoint required");
        return pool->Endpoint();
    }
    template <class T>
    std::future<T> Request(kvcache::Command command, const std::string& key, const std::string& value = "") {
        if (key.size() > kvcache::MAX_KEY_SIZE ||
            value.size() >
                (command == kvcache::Command::SET_TTL ? kvcache::MAX_WIRE_VALUE_SIZE : kvcache::MAX_VALUE_SIZE) ||
            kvcache::HEADER_SIZE + key.size() + value.size() > options_.max_frame_bytes)
            throw Error(ErrorCode::invalid_argument, "KVCache request exceeds frame limit");
        auto request = kvcache::Message::encode(command, key, value);
        auto promise = std::make_shared<std::promise<T>>();
        auto result = promise->get_future();
        transport_->Submit(
            next_.fetch_add(1), std::move(request),
            [promise, limit = options_.max_frame_bytes, command, key](auto response, std::exception_ptr error) -> bool {
                try {
                    if (error) {
                        promise->set_exception(std::move(error));
                        return true;
                    }
                    auto h = kvcache::Message::decodeHeader(response.data());
                    if (h.command != static_cast<uint8_t>(command) || h.key_len != key.size() ||
                        response.size() > limit ||
                        ((command == kvcache::Command::SET || command == kvcache::Command::SET_TTL ||
                          command == kvcache::Command::DEL) &&
                         h.value_len))
                        throw Error(ErrorCode::protocol, "Unexpected KVCache response");
                    std::string response_key(reinterpret_cast<const char*>(response.data() + kvcache::HEADER_SIZE),
                                             h.key_len);
                    std::string value(reinterpret_cast<const char*>(response.data() + kvcache::HEADER_SIZE + h.key_len),
                                      h.value_len);
                    if (response_key != key) throw Error(ErrorCode::protocol, "KVCache response key mismatch");
                    if constexpr (std::is_same_v<T, bool>)
                        promise->set_value(true);
                    else if constexpr (std::is_same_v<T, std::optional<std::string>>) {
                        wire::CacheValue item;
                        if (!item.ParseFromString(value)) throw Error(ErrorCode::protocol, "Invalid lookup result");
                        promise->set_value(item.found() ? std::optional<std::string>(item.value()) : std::nullopt);
                    } else
                        promise->set_value(std::move(value));
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
    ClientOptions options_;
    std::atomic<uint64_t> next_{1};
    std::unique_ptr<AsyncTransport> transport_;
};
}  // namespace kvrpc
