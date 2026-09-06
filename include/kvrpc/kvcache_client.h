#pragma once
#include "kvrpc/connection_pool.h"
#include "kvrpc/executor.h"
#include "kvrpc/kv_protocol.h"

namespace kvrpc {
class KVCacheClient {
public:
    explicit KVCacheClient(std::shared_ptr<ConnectionPool> pool, ClientOptions options = {})
        : pool_(std::move(pool)), options_(options), executor_(options) {
        if (!pool_) throw Error(ErrorCode::invalid_argument, "A connection pool is required");
    }
    std::future<bool> Set(const std::string& key, const std::string& value) {
        return Request<bool>(kvcache::Command::SET, key, value);
    }
    // Version 1 represents both missing keys and empty values as an empty string.
    std::future<std::string> Get(const std::string& key) { return Request<std::string>(kvcache::Command::GET, key); }
    std::future<bool> Delete(const std::string& key) { return Request<bool>(kvcache::Command::DEL, key); }
    std::future<std::string> Stats() { return Request<std::string>(kvcache::Command::STATS, ""); }
private:
    template<class T>
    std::future<T> Request(kvcache::Command command, const std::string& key, const std::string& value = "") {
        if (key.size() > kvcache::MAX_KEY_SIZE || value.size() > kvcache::MAX_VALUE_SIZE ||
            kvcache::HEADER_SIZE + key.size() + value.size() > options_.max_frame_bytes)
            throw Error(ErrorCode::invalid_argument, "KVCache request exceeds frame limit");
        auto request = kvcache::Message::encode(command, key, value);
        return executor_.Submit([pool = pool_, limit = options_.max_frame_bytes, command, key,
                                 request = std::move(request)]() -> T {
            auto conn = pool->Acquire();
            try {
                auto check = [&](bool ok) {
                    if (!ok) throw Error(conn->TimedOut() ? ErrorCode::timeout : ErrorCode::transport, "KVCache transport failed");
                };
                check(conn->SendAll(reinterpret_cast<const char*>(request.data()), request.size()));
                uint8_t bytes[kvcache::HEADER_SIZE];
                check(conn->RecvAll(reinterpret_cast<char*>(bytes), sizeof(bytes)));
                auto h = kvcache::Message::decodeHeader(bytes);
                try { kvcache::Message::validate(h); }
                catch (const std::exception&) { throw Error(ErrorCode::protocol, "Invalid KVCache response header"); }
                if (h.command != static_cast<uint8_t>(command) || h.key_len != key.size() ||
                    kvcache::HEADER_SIZE + size_t(h.key_len) + h.value_len > limit ||
                    ((command == kvcache::Command::SET || command == kvcache::Command::DEL) && h.value_len))
                    throw Error(ErrorCode::protocol, "Unexpected KVCache response");
                std::string response_key(h.key_len, '\0'), response_value(h.value_len, '\0');
                check(conn->RecvAll(response_key.data(), response_key.size()));
                check(conn->RecvAll(response_value.data(), response_value.size()));
                if (response_key != key) throw Error(ErrorCode::protocol, "KVCache response key mismatch");
                if constexpr (std::is_same_v<T, bool>) return true;
                else return response_value;
            } catch (...) { conn->Close(); throw; }
        });
    }
    std::shared_ptr<ConnectionPool> pool_;
    ClientOptions options_;
    Executor executor_;
};
}  // namespace kvrpc
