#pragma once
#include "aof.h"
#include "sharded_cache.h"
#include <mutex>

namespace kvcache {
// One serialization point keeps live mutation order identical to AOF replay order.
class CacheService {
public:
    CacheService(size_t capacity, size_t shards, const std::string& path,
                 size_t max_aof_bytes = 1024ULL * 1024 * 1024)
        : cache_(capacity, shards), aof_(path, max_aof_bytes) {
        aof_.replay([this](Command cmd, const std::string& key, const std::string& value) {
            if (cmd == Command::SET) cache_.put(key, value);
            else cache_.remove(key);
        });
        aof_.start();
    }
    std::vector<uint8_t> Handle(const std::vector<uint8_t>& data, size_t& consumed) {
        consumed = 0;
        if (data.size() < HEADER_SIZE) return {};
        auto h = Message::decodeHeader(data.data());
        Message::validate(h);
        auto command = static_cast<Command>(h.command);
        if ((command != Command::SET && h.value_len) || (command == Command::STATS && h.key_len))
            throw std::runtime_error("Invalid request body for command");
        size_t total = HEADER_SIZE + size_t(h.key_len) + h.value_len;
        if (data.size() < total) return {};
        std::string key(reinterpret_cast<const char*>(data.data() + HEADER_SIZE), h.key_len);
        std::string value(reinterpret_cast<const char*>(data.data() + HEADER_SIZE + h.key_len), h.value_len);
        std::lock_guard<std::mutex> lock(mutex_);
        if (failed_) throw std::runtime_error("Storage is unavailable; restart after resolving the failure");
        std::string response;
        if (command == Command::SET || command == Command::DEL) {
            try {
                aof_.log(command, key, value);
                if (command == Command::SET) cache_.put(key, value);
                else cache_.remove(key);
            } catch (...) { failed_ = true; throw; }
        } else if (command == Command::GET) {
            response = cache_.get(key).value_or("");
        } else {
            auto stats = cache_.getStats();
            response = "Hits: " + std::to_string(stats.hits) + ", Misses: " + std::to_string(stats.misses);
        }
        consumed = total;
        return Message::encode(command, key, response);
    }
private:
    ShardedCache<std::string, std::string> cache_;
    AofLogger aof_;
    std::mutex mutex_;
    bool failed_ = false;
};
}  // namespace kvcache
