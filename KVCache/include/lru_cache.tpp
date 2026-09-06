#pragma once

#include "lru_cache.h"
#include <stdexcept>

namespace kvcache {

template <typename Key, typename Value>
LRUCache<Key, Value>::LRUCache(size_t capacity) : capacity_(capacity) {
    if (!capacity) throw std::invalid_argument("Cache capacity must be positive");
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

    // Allocate the new entry before eviction; preserve list/map consistency on failure.
    items_.emplace_front(key, value);
    try { cache_map_.emplace(key, items_.begin()); }
    catch (...) { items_.pop_front(); throw; }
    if (items_.size() > capacity_) {
        cache_map_.erase(items_.back().first);
        items_.pop_back();
    }
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
bool LRUCache<Key, Value>::remove(const Key& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_map_.find(key);
    if (it == cache_map_.end()) return false;
    items_.erase(it->second);
    cache_map_.erase(it);
    return true;
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
