#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
namespace kvrpc {
class Buffer {
   public:
    size_t size() const { return bytes_.size() - read_; }
    const uint8_t* data() const { return bytes_.data() + read_; }
    void append(const uint8_t* data, size_t size) {
        if (read_ && read_ >= bytes_.size() / 2) {
            bytes_.erase(bytes_.begin(), bytes_.begin() + read_);
            read_ = 0;
        }
        bytes_.insert(bytes_.end(), data, data + size);
    }
    void consume(size_t n) {
        read_ += n;
        if (read_ == bytes_.size()) {
            bytes_.clear();
            read_ = 0;
        }
    }
    std::vector<uint8_t> copy(size_t n) const { return {data(), data() + n}; }

   private:
    std::vector<uint8_t> bytes_;
    size_t read_ = 0;
};
}  // namespace kvrpc
