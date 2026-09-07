#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

#include "lru_cache.h"

namespace kvcache {

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class ShardedCache {
public:
    ShardedCache(size_t capacity, size_t num_shards = 16, size_t max_bytes = SIZE_MAX)
        : num_shards_(num_shards), hash_() {
        if (!capacity || !num_shards) throw std::invalid_argument("Capacity and shard count must be positive");
        num_shards_ = std::min(capacity, num_shards);
        for (size_t i = 0; i < num_shards_; ++i) {
            shards_.emplace_back(std::make_unique<LRUCache<Key, Value>>(
                capacity / num_shards_ + (i < capacity % num_shards_ ? 1 : 0),
                max_bytes / num_shards_ + (i < max_bytes % num_shards_ ? 1 : 0)));
        }
    }

    bool canStore(const Key& key, const Value& value) {
        return getShard(key).canStore(key, value);
    }
    void put(const Key& key, const Value& value, uint64_t expires = 0) {
        getShard(key).put(key, value, expires);
    }

    std::optional<Value> get(const Key& key) {
        return getShard(key).get(key);
    }

    bool remove(const Key& key) {
        return getShard(key).remove(key);
    }

    bool exists(const Key& key) {
        return getShard(key).exists(key);
    }

    size_t size() const {
        size_t total = 0;
        for (const auto& shard : shards_) {
            total += shard->size();
        }
        return total;
    }

    struct Stats {
        size_t hits = 0;
        size_t misses = 0;
        size_t bytes = 0, evictions = 0;
    };

    Stats getStats() const {
        Stats total;
        for (const auto& shard : shards_) {
            auto s = shard->getStats();
            total.hits += s.hits;
            total.misses += s.misses;
            total.bytes += s.bytes;
            total.evictions += s.evictions;
        }
        return total;
    }

    std::vector<typename LRUCache<Key, Value>::Entry> snapshot() const {
        std::vector<typename LRUCache<Key, Value>::Entry> result;
        for (const auto& shard : shards_) {
            auto entries = shard->snapshot();
            result.insert(result.end(), entries.begin(), entries.end());
        }
        return result;
    }

private:
    LRUCache<Key, Value>& getShard(const Key& key) {
        size_t hash_value = hash_(key);
        return *shards_[hash_value % num_shards_];
    }

    size_t num_shards_;
    std::vector<std::unique_ptr<LRUCache<Key, Value>>> shards_;
    Hash hash_;
};

}  // namespace kvcache
