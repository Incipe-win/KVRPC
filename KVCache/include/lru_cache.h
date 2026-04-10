#pragma once

#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace kvcache {

template <typename Key, typename Value>
class LRUCache {
public:
    explicit LRUCache(size_t capacity);

    // Disable copy
    LRUCache(const LRUCache&) = delete;
    LRUCache& operator=(const LRUCache&) = delete;

    // Basic operations
    void put(const Key& key, const Value& value);
    std::optional<Value> get(const Key& key);
    bool exists(const Key& key);
    size_t size() const;

    // Stats
    struct Stats {
        size_t hits = 0;
        size_t misses = 0;
    };
    Stats getStats() const;

private:
    size_t capacity_;
    std::list<std::pair<Key, Value>> items_;  // Doubly linked list: Most recent at front
    std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator> cache_map_;
    mutable std::mutex mutex_;  // Protects items_ and cache_map_
    Stats stats_;
};

}  // namespace kvcache

#include "lru_cache.tpp"  // Template implementation
