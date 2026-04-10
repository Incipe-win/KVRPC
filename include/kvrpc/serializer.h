#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace kvrpc {

class Serializer {
private:
  std::vector<char> buffer_;
  size_t offset_ = 0;

public:
  Serializer() = default;
  Serializer(const std::vector<char> &buf) : buffer_(buf), offset_(0) {}
  Serializer(std::vector<char> &&buf) : buffer_(std::move(buf)), offset_(0) {}

  const std::vector<char> &GetBuffer() const { return buffer_; }
  void Reset() {
    buffer_.clear();
    offset_ = 0;
  }

  // 1. Serialization for Arithmetic types (int, double, bool, etc.)
  template <typename T>
  typename std::enable_if<std::is_arithmetic<T>::value>::type
  Serialize(const T &value) {
    const char *p = reinterpret_cast<const char *>(&value);
    buffer_.insert(buffer_.end(), p, p + sizeof(T));
  }

  // 2. Serialization for std::string
  void Serialize(const std::string &str) {
    uint32_t len = str.size();
    Serialize(len);
    buffer_.insert(buffer_.end(), str.data(), str.data() + len);
  }

  // 3. Serialization for C-style strings
  void Serialize(const char *str) { Serialize(std::string(str)); }

  // 4. Variadic Template Serialization for multiple arguments
  template <typename T, typename... Args>
  void Serialize(const T &first, const Args &...args) {
    Serialize(first);
    Serialize(args...);
  }

  // 1. Deserialization for Arithmetic types
  template <typename T>
  typename std::enable_if<std::is_arithmetic<T>::value>::type
  Deserialize(T &value) {
    if (offset_ + sizeof(T) > buffer_.size()) {
      throw std::out_of_range("buffer overflow during deserialization");
    }
    std::memcpy(&value, buffer_.data() + offset_, sizeof(T));
    offset_ += sizeof(T);
  }

  // 2. Deserialization for std::string
  void Deserialize(std::string &str) {
    uint32_t len;
    Deserialize(len);
    if (offset_ + len > buffer_.size()) {
      throw std::out_of_range("buffer overflow during string deserialization");
    }
    str.assign(buffer_.data() + offset_, len);
    offset_ += len;
  }

  // 3. Variadic Template Deserialization for multiple arguments
  template <typename T, typename... Args>
  void Deserialize(T &first, Args &...args) {
    Deserialize(first);
    Deserialize(args...);
  }
};

} // namespace kvrpc
