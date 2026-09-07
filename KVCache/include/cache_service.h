#pragma once
#include <mutex>

#include "aof.h"
#include "rpc.pb.h"
#include "sharded_cache.h"
#include "tcp_server.h"

namespace kvcache {
// Requests have one serialization order; fsync completes before cache visibility and replies.
class CacheService {
public:
    CacheService(size_t capacity, size_t shards, const std::string& path, size_t max_aof_bytes = 1024ULL * 1024 * 1024,
                 size_t max_cache_bytes = 64 * 1024 * 1024)
        : cache_(capacity, shards, max_cache_bytes), aof_(path, max_aof_bytes) {
        aof_.replay([this](Command cmd, const std::string& key, const std::string& value) {
            if (cmd == Command::SET)
                cache_.put(key, value);
            else if (cmd == Command::SET_TTL) {
                kvrpc::wire::CacheValue item;
                if (!item.ParseFromString(value) || !item.expires_unix_ms())
                    throw std::runtime_error("Invalid TTL log record");
                cache_.put(key, item.value(), item.expires_unix_ms());
            } else
                cache_.remove(key);
        });
        aof_.start();
    }
    std::vector<uint8_t> Handle(const std::vector<uint8_t>& data, size_t& consumed) {
        consumed = 0;
        auto reply = HandleBatch({std::cref(data)}).front();
        if (reply.close) throw std::runtime_error("Invalid request or unavailable storage");
        consumed = reply.consumed;
        return std::move(reply.bytes);
    }
    std::vector<TcpServer::Reply> HandleBatch(const std::vector<TcpServer::Input>& inputs) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (failed_) throw std::runtime_error("Storage is unavailable; restart after resolving the failure");
        struct Request {
            Command command = Command::GET;
            std::string key, value;
            uint64_t expires = 0;
        };
        std::vector<Request> requests(inputs.size());
        std::vector<TcpServer::Reply> replies(inputs.size());
        std::vector<AofLogger::Mutation> mutations;
        for (size_t i = 0; i < inputs.size(); ++i) {
            const auto& data = inputs[i].get();
            try {
                if (data.size() < HEADER_SIZE) continue;
                auto h = Message::decodeHeader(data.data());
                Message::validate(h);
                auto command = static_cast<Command>(h.command);
                if ((command != Command::SET && command != Command::SET_TTL && h.value_len) ||
                    (command == Command::STATS && h.key_len))
                    throw std::runtime_error("Invalid request body for command");
                size_t total = HEADER_SIZE + size_t(h.key_len) + h.value_len;
                if (data.size() < total) continue;
                requests[i] = {
                    command, std::string(reinterpret_cast<const char*>(data.data() + HEADER_SIZE), h.key_len),
                    std::string(reinterpret_cast<const char*>(data.data() + HEADER_SIZE + h.key_len), h.value_len)};
                auto& request = requests[i];
                if (command == Command::SET_TTL) {
                    kvrpc::wire::CacheValue item;
                    if (!item.ParseFromString(request.value) || !item.ttl_ms() || item.ttl_ms() > 315360000000ULL)
                        throw std::runtime_error("Invalid TTL request");
                    request.value = item.value();
                    request.expires = UnixMilliseconds() + item.ttl_ms();
                    item.clear_ttl_ms();
                    item.set_expires_unix_ms(request.expires);
                    if (request.value.size() > MAX_VALUE_SIZE || !cache_.canStore(request.key, request.value))
                        throw std::runtime_error("Entry exceeds byte limit");
                    mutations.push_back({command, request.key, item.SerializeAsString()});
                }
                if (command == Command::SET &&
                    (request.value.size() > MAX_VALUE_SIZE || !cache_.canStore(request.key, request.value)))
                    throw std::runtime_error("Entry exceeds byte limit");
                if (command == Command::SET || command == Command::DEL)
                    mutations.push_back({command, requests[i].key, requests[i].value});
                replies[i].consumed = total;
            } catch (const std::runtime_error&) {
                replies[i].close = true;
            }
        }
        try {
            if (aof_.needsRewrite(mutations)) CompactLocked();
            aof_.logBatch(mutations);
            for (size_t i = 0; i < requests.size(); ++i) {
                if (!replies[i].consumed || replies[i].close) continue;
                auto& request = requests[i];
                std::string response;
                if (request.command == Command::SET || request.command == Command::SET_TTL)
                    cache_.put(request.key, request.value, request.expires);
                else if (request.command == Command::DEL)
                    cache_.remove(request.key);
                else if (request.command == Command::GET)
                    response = cache_.get(request.key).value_or("");
                else if (request.command == Command::LOOKUP) {
                    auto value = cache_.get(request.key);
                    kvrpc::wire::CacheValue item;
                    item.set_found(value.has_value());
                    if (value) item.set_value(*value);
                    response = item.SerializeAsString();
                } else {
                    auto stats = cache_.getStats();
                    auto disk = aof_.stats();
                    response = "Hits: " + std::to_string(stats.hits) + ", Misses: " + std::to_string(stats.misses) +
                               ", AOF records: " + std::to_string(disk.records) +
                               ", AOF syncs: " + std::to_string(disk.syncs) +
                               ", AOF bytes: " + std::to_string(disk.bytes) +
                               ", Cache bytes: " + std::to_string(stats.bytes) +
                               ", Evictions: " + std::to_string(stats.evictions) +
                               ", AOF rewrites: " + std::to_string(disk.rewrites) +
                               ", AOF sync us: " + std::to_string(disk.sync_us);
                }
                if (request.command == Command::STATS && metrics_) response += metrics_();
                replies[i].bytes = Message::encode(request.command, request.key, response);
            }
        } catch (const AofCapacityError&) {
            throw;
        } catch (...) {
            failed_ = true;
            throw;
        }
        return replies;
    }
    void SetTransportMetrics(std::function<std::string()> callback) {
        metrics_ = std::move(callback);
    }
    void Compact() {
        std::lock_guard<std::mutex> lock(mutex_);
        CompactLocked();
    }

private:
    void CompactLocked() {
        std::vector<AofLogger::Mutation> snapshot;
        for (const auto& entry : cache_.snapshot()) {
            if (entry.expires) {
                kvrpc::wire::CacheValue value;
                value.set_value(entry.value);
                value.set_expires_unix_ms(entry.expires);
                snapshot.push_back({Command::SET_TTL, entry.key, value.SerializeAsString()});
            } else
                snapshot.push_back({Command::SET, entry.key, entry.value});
        }
        aof_.rewrite(snapshot);
    }
    ShardedCache<std::string, std::string> cache_;
    AofLogger aof_;
    std::mutex mutex_;
    bool failed_ = false;
    std::function<std::string()> metrics_;
};
}  // namespace kvcache
