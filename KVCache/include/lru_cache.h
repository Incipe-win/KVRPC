#pragma once
#include <chrono>
#include <list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>
namespace kvcache {
inline uint64_t UnixMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
template <class Key, class Value>
class LRUCache {
public:
    struct Entry {
        Key key;
        Value value;
        uint64_t expires = 0;
        size_t bytes = 0;
    };
    struct Stats {
        size_t hits = 0, misses = 0, bytes = 0, evictions = 0;
    };
    explicit LRUCache(size_t capacity, size_t max_bytes = SIZE_MAX) : capacity_(capacity), max_bytes_(max_bytes) {
        if (!capacity || !max_bytes) throw std::invalid_argument("Cache limits must be positive");
    }
    bool canStore(const Key& key, const Value& value) const {
        return Cost(key) + Cost(value) + sizeof(Entry) <= max_bytes_;
    }
    void put(const Key& key, const Value& value, uint64_t expires = 0) {
        size_t bytes = Cost(key) + Cost(value) + sizeof(Entry);
        if (bytes > max_bytes_) throw std::length_error("Entry exceeds shard byte budget");
        std::lock_guard<std::mutex> lock(mutex_);
        auto old = index_.find(key);
        if (expires && expires <= UnixMilliseconds()) {
            if (old != index_.end()) Erase(old->second);
            return;
        }
        items_.push_front({key, value, expires, bytes});
        if (old != index_.end()) {
            stats_.bytes -= old->second->bytes;
            items_.erase(old->second);
            old->second = items_.begin();
        } else {
            try {
                index_.emplace(key, items_.begin());
            } catch (...) {
                items_.pop_front();
                throw;
            }
        }
        stats_.bytes += bytes;
        while (items_.size() > capacity_ || stats_.bytes > max_bytes_) {
            Erase(std::prev(items_.end()));
            ++stats_.evictions;
        }
    }
    std::optional<Value> get(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = index_.find(key);
        if (it != index_.end() && Expired(*it->second)) {
            Erase(it->second);
            it = index_.end();
        }
        if (it == index_.end()) {
            ++stats_.misses;
            return {};
        }
        ++stats_.hits;
        items_.splice(items_.begin(), items_, it->second);
        return it->second->value;
    }
    bool exists(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = index_.find(key);
        if (it == index_.end()) return false;
        if (Expired(*it->second)) {
            Erase(it->second);
            return false;
        }
        return true;
    }
    bool remove(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = index_.find(key);
        if (it == index_.end()) return false;
        Erase(it->second);
        return true;
    }
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }
    Stats getStats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }
    // Oldest first so replay preserves the snapshot's LRU order within each shard.
    std::vector<Entry> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Entry> result;
        for (auto it = items_.rbegin(); it != items_.rend(); ++it)
            if (!Expired(*it)) result.push_back(*it);
        return result;
    }

private:
    template <class T>
    static size_t Cost(const T& value) {
        if constexpr (std::is_same_v<T, std::string>)
            return value.size();
        else
            return sizeof(T);
    }
    static bool Expired(const Entry& item) {
        return item.expires && item.expires <= UnixMilliseconds();
    }
    void Erase(typename std::list<Entry>::iterator it) {
        stats_.bytes -= it->bytes;
        index_.erase(it->key);
        items_.erase(it);
    }
    size_t capacity_, max_bytes_;
    std::list<Entry> items_;
    std::unordered_map<Key, typename std::list<Entry>::iterator> index_;
    mutable std::mutex mutex_;
    Stats stats_;
};
}  // namespace kvcache
