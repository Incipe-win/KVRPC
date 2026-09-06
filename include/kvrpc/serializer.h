#pragma once
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace kvrpc {
// Scalars and string lengths use little endian, preserving the original x86 wire format.
// Use fixed-width integers in portable RPC signatures; long double is unsupported.
class Serializer {
public:
    explicit Serializer(size_t max_bytes = 64 * 1024 * 1024) : max_bytes_(max_bytes) {}

    explicit Serializer(const std::vector<char>& buffer) : buffer_(buffer) {}
    explicit Serializer(std::vector<char>&& buffer) : buffer_(std::move(buffer)) {}
    const std::vector<char>& GetBuffer() const noexcept { return buffer_; }
    size_t Remaining() const noexcept { return buffer_.size() - offset_; }
    void Reset() { buffer_.clear(); offset_ = 0; }
    void Serialize() {}
    void Deserialize() {}
    template<class T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
    void Serialize(const T& value) {
        static_assert(sizeof(T) <= 8, "Only arithmetic types of up to 8 bytes are supported");
        static_assert(!std::is_floating_point_v<T> || std::numeric_limits<T>::is_iec559, "IEEE 754 required");
        ReserveFor(sizeof(T));
        if constexpr (std::is_same_v<T, bool>) buffer_.push_back(value ? 1 : 0);
        else {
            unsigned char bytes[sizeof(T)];
            std::memcpy(bytes, &value, sizeof(T));
            for (size_t i = 0; i < sizeof(T); ++i)
                buffer_.push_back(static_cast<char>(bytes[LittleEndian() ? i : sizeof(T) - 1 - i]));
        }
    }
    void Serialize(const std::string& value) {
        if (value.size() > UINT32_MAX) throw std::length_error("String exceeds wire length limit");
        ReserveFor(sizeof(uint32_t) + value.size());
        Serialize(static_cast<uint32_t>(value.size()));
        buffer_.insert(buffer_.end(), value.begin(), value.end());
    }
    void Serialize(const char* value) {
        if (!value) throw std::invalid_argument("Cannot serialize a null string");
        Serialize(std::string(value));
    }
    template<class T, class U, class... Rest>
    void Serialize(const T& first, const U& second, const Rest&... rest) {
        Serialize(first); Serialize(second, rest...);
    }
    template<class T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
    void Deserialize(T& value) {
        static_assert(sizeof(T) <= 8, "Only arithmetic types of up to 8 bytes are supported");
        static_assert(!std::is_floating_point_v<T> || std::numeric_limits<T>::is_iec559, "IEEE 754 required");
        Require(sizeof(T));
        if constexpr (std::is_same_v<T, bool>) {
            auto byte = static_cast<unsigned char>(buffer_[offset_]);
            if (byte > 1) throw std::invalid_argument("Invalid boolean encoding");
            value = byte == 1;
        } else {
            unsigned char bytes[sizeof(T)];
            for (size_t i = 0; i < sizeof(T); ++i)
                bytes[LittleEndian() ? i : sizeof(T) - 1 - i] = static_cast<unsigned char>(buffer_[offset_ + i]);
            std::memcpy(&value, bytes, sizeof(T));
        }
        offset_ += sizeof(T);
    }
    void Deserialize(std::string& value) {
        uint32_t size = 0; Deserialize(size); Require(size);
        value.assign(buffer_.data() + offset_, size); offset_ += size;
    }
    template<class T, class U, class... Rest>
    void Deserialize(T& first, U& second, Rest&... rest) { Deserialize(first); Deserialize(second, rest...); }
private:
    static bool LittleEndian() { const uint16_t n = 1; return *reinterpret_cast<const unsigned char*>(&n) == 1; }
    void Require(size_t bytes) const {
        if (bytes > Remaining()) throw std::out_of_range("Truncated serialized value");
    }
    void ReserveFor(size_t bytes) const {
        if (buffer_.size() > max_bytes_ || bytes > max_bytes_ - buffer_.size())
            throw std::length_error("Serialized payload exceeds configured limit");
    }
    size_t max_bytes_ = 64 * 1024 * 1024;
    std::vector<char> buffer_;
    size_t offset_ = 0;
};
}  // namespace kvrpc
