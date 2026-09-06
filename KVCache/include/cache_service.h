#pragma once
#include "aof.h"
#include "sharded_cache.h"
#include "tcp_server.h"
#include <mutex>

namespace kvcache {
// Requests have one serialization order; fsync completes before cache visibility and replies.
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
        auto reply = HandleBatch({std::cref(data)}).front();
        if (reply.close) throw std::runtime_error("Invalid request or unavailable storage");
        consumed = reply.consumed;
        return std::move(reply.bytes);
    }
    std::vector<TcpServer::Reply> HandleBatch(const std::vector<TcpServer::Input>& inputs) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (failed_) throw std::runtime_error("Storage is unavailable; restart after resolving the failure");
        struct Request { Command command = Command::GET; std::string key, value; };
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
                if ((command != Command::SET && h.value_len) || (command == Command::STATS && h.key_len))
                    throw std::runtime_error("Invalid request body for command");
                size_t total = HEADER_SIZE + size_t(h.key_len) + h.value_len;
                if (data.size() < total) continue;
                requests[i] = {command,
                    std::string(reinterpret_cast<const char*>(data.data() + HEADER_SIZE), h.key_len),
                    std::string(reinterpret_cast<const char*>(data.data() + HEADER_SIZE + h.key_len), h.value_len)};
                if (command == Command::SET || command == Command::DEL)
                    mutations.push_back({command, requests[i].key, requests[i].value});
                replies[i].consumed = total;
            } catch (const std::runtime_error&) { replies[i].close = true; }
        }
        try {
            aof_.logBatch(mutations);
            for (size_t i = 0; i < requests.size(); ++i) {
                if (!replies[i].consumed || replies[i].close) continue;
                auto& request = requests[i];
                std::string response;
                if (request.command == Command::SET) cache_.put(request.key, request.value);
                else if (request.command == Command::DEL) cache_.remove(request.key);
                else if (request.command == Command::GET) response = cache_.get(request.key).value_or("");
                else {
                    auto stats = cache_.getStats();
                    auto disk = aof_.stats();
                    response = "Hits: " + std::to_string(stats.hits) + ", Misses: " + std::to_string(stats.misses) +
                        ", AOF records: " + std::to_string(disk.records) + ", AOF syncs: " + std::to_string(disk.syncs) +
                        ", AOF bytes: " + std::to_string(disk.bytes);
                }
                replies[i].bytes = Message::encode(request.command, request.key, response);
            }
        } catch (...) { failed_ = true; throw; }
        return replies;
    }
private:
    ShardedCache<std::string, std::string> cache_;
    AofLogger aof_;
    std::mutex mutex_;
    bool failed_ = false;
};
}  // namespace kvcache
