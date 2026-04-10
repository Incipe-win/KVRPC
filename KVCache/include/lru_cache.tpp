#pragma once

#include "lru_cache.h"

namespace kvcache {

template <typename Key, typename Value>
LRUCache<Key, Value>::LRUCache(size_t capacity) : capacity_(capacity) {
}

template <typename Key, typename Value>
void LRUCache<Key, Value>::put(const Key& key, const Value& value) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = cache_map_.find(key);
    if (it != cache_map_.end()) {
        // Update value and move to front
        it->second->second = value;
        items_.splice(items_.begin(), items_, it->second);
        return;
    }

    // Insert new item
    if (items_.size() >= capacity_) {
        // Evict least recently used (back)
        auto last = items_.back();
        cache_map_.erase(last.first);
        items_.pop_back();
    }

    items_.emplace_front(key, value);
    cache_map_[key] = items_.begin();
}

template <typename Key, typename Value>
std::optional<Value> LRUCache<Key, Value>::get(const Key& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = cache_map_.find(key);
    if (it == cache_map_.end()) {
        stats_.misses++;
        return std::nullopt;
    }

    stats_.hits++;
    // Move to front (mark as recently used)
    items_.splice(items_.begin(), items_, it->second);
    return it->second->second;
}

template <typename Key, typename Value>
bool LRUCache<Key, Value>::exists(const Key& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_map_.find(key) != cache_map_.end();
}

template <typename Key, typename Value>
size_t LRUCache<Key, Value>::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_.size();
}

template <typename Key, typename Value>
typename LRUCache<Key, Value>::Stats LRUCache<Key, Value>::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

}  // namespace kvcache
