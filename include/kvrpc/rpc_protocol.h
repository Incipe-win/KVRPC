#pragma once
#include <arpa/inet.h>

#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#include "kvrpc/error.h"
#include "rpc.pb.h"
namespace kvrpc {
inline constexpr size_t RPC_HEADER = 16;
inline size_t RpcFrameSize(const uint8_t* bytes, size_t size) {
    if (size < RPC_HEADER) return 0;
    if (std::memcmp(bytes, "KVR2", 4)) throw Error(ErrorCode::protocol, "Invalid RPC version");
    uint32_t n;
    std::memcpy(&n, bytes + 4, 4);
    return RPC_HEADER + size_t(ntohl(n));
}
inline uint64_t RpcId(const std::vector<uint8_t>& bytes) {
    uint64_t id = 0;
    for (size_t i = 8; i < RPC_HEADER; ++i) id = (id << 8) | bytes[i];
    if (!id) throw Error(ErrorCode::protocol, "Zero request identifier");
    return id;
}
inline std::vector<uint8_t> RpcEncode(uint64_t id, const wire::Envelope& message, size_t limit) {
    auto size = message.ByteSizeLong();
    if (size > limit) throw Error(ErrorCode::invalid_argument, "RPC payload exceeds limit");
    std::vector<uint8_t> bytes(RPC_HEADER + size);
    std::memcpy(bytes.data(), "KVR2", 4);
    uint32_t n = htonl(static_cast<uint32_t>(size));
    std::memcpy(bytes.data() + 4, &n, 4);
    for (int i = 15; i >= 8; --i) {
        bytes[i] = id & 255;
        id >>= 8;
    }
    message.SerializeToArray(bytes.data() + RPC_HEADER, static_cast<int>(size));
    return bytes;
}
inline wire::Envelope RpcDecode(const std::vector<uint8_t>& bytes) {
    wire::Envelope result;
    if (bytes.size() < RPC_HEADER || RpcFrameSize(bytes.data(), bytes.size()) != bytes.size() ||
        !result.ParseFromArray(bytes.data() + RPC_HEADER, static_cast<int>(bytes.size() - RPC_HEADER)))
        throw Error(ErrorCode::protocol, "Invalid RPC envelope");
    return result;
}
template <class T>
void ToValue(wire::Value& out, const T& value) {
    using V = std::decay_t<T>;
    if constexpr (std::is_same_v<V, bool>)
        out.set_bool_value(value);
    else if constexpr (std::is_integral_v<V> && std::is_signed_v<V>)
        out.set_signed_value(value);
    else if constexpr (std::is_integral_v<V>)
        out.set_unsigned_value(value);
    else if constexpr (std::is_floating_point_v<V>)
        out.set_real_value(value);
    else if constexpr (std::is_base_of_v<google::protobuf::MessageLite, V>)
        out.set_message_value(value.SerializeAsString());
    else {
        if constexpr (std::is_pointer_v<V>) {
            if (!value) throw Error(ErrorCode::invalid_argument, "Null string argument");
        }
        out.set_string_value(value);
    }
}
template <class T>
T FromValue(const wire::Value& value) {
    T out{};
    bool valid = false;
    if constexpr (std::is_same_v<T, bool>) {
        valid = value.has_bool_value();
        out = value.bool_value();
    } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
        valid = value.has_signed_value() && value.signed_value() >= std::numeric_limits<T>::min() &&
                value.signed_value() <= std::numeric_limits<T>::max();
        out = static_cast<T>(value.signed_value());
    } else if constexpr (std::is_integral_v<T>) {
        valid = value.has_unsigned_value() && value.unsigned_value() <= std::numeric_limits<T>::max();
        out = static_cast<T>(value.unsigned_value());
    } else if constexpr (std::is_floating_point_v<T>) {
        valid = value.has_real_value();
        out = static_cast<T>(value.real_value());
    } else if constexpr (std::is_base_of_v<google::protobuf::MessageLite, T>) {
        valid = value.has_message_value() && out.ParseFromString(value.message_value());
    } else {
        valid = value.has_string_value();
        out = value.string_value();
    }
    if (!valid) throw Error(ErrorCode::protocol, "RPC value type mismatch");
    return out;
}
}  // namespace kvrpc
