#pragma once

#include <functional>
#include <algorithm>
#include <stdexcept>
#include <memory>
#include <vector>

#include "lru_cache.h"

namespace kvcache {

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class ShardedCache {
public:
    ShardedCache(size_t capacity, size_t num_shards = 16) : num_shards_(num_shards), hash_() {
        if (!capacity || !num_shards) throw std::invalid_argument("Capacity and shard count must be positive");
        num_shards_ = std::min(capacity, num_shards);
        for (size_t i = 0; i < num_shards_; ++i) {
            shards_.emplace_back(std::make_unique<LRUCache<Key, Value>>(
                capacity / num_shards_ + (i < capacity % num_shards_ ? 1 : 0)));
        }
    }

    void put(const Key& key, const Value& value) {
        getShard(key).put(key, value);
    }

    std::optional<Value> get(const Key& key) {
        return getShard(key).get(key);
    }

    bool remove(const Key& key) { return getShard(key).remove(key); }

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
    };

    Stats getStats() const {
        Stats total;
        for (const auto& shard : shards_) {
            auto s = shard->getStats();
            total.hits += s.hits;
            total.misses += s.misses;
        }
        return total;
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
