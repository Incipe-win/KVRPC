#pragma once
#include <arpa/inet.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace kvcache {
inline constexpr uint16_t MAGIC = 0xCAFE;
inline constexpr uint8_t VERSION = 1;
inline constexpr size_t HEADER_SIZE = 12;
inline constexpr size_t MAX_KEY_SIZE = 64 * 1024;
inline constexpr size_t MAX_VALUE_SIZE = 1024 * 1024;
inline constexpr size_t MAX_WIRE_VALUE_SIZE = MAX_VALUE_SIZE + 64;
inline constexpr size_t MAX_FRAME_SIZE = HEADER_SIZE + MAX_KEY_SIZE + MAX_WIRE_VALUE_SIZE;
enum class Command : uint8_t { SET = 1, GET = 2, DEL = 3, STATS = 4, LOOKUP = 5, SET_TTL = 6, UNKNOWN = 0 };
struct Header {
    uint16_t magic;
    uint8_t version;
    uint8_t command;
    uint32_t key_len;
    uint32_t value_len;
};
struct Message {
    Header header;
    std::string key;
    std::string value;
    static void validate(const Header& h) {
        if (h.magic != MAGIC || h.version != VERSION || h.command < 1 || h.command > 6 || h.key_len > MAX_KEY_SIZE ||
            h.value_len > MAX_WIRE_VALUE_SIZE)
            throw std::runtime_error("Invalid KVCache frame header");
    }
    static std::vector<uint8_t> encode(Command command, const std::string& key, const std::string& value = "") {
        if (key.size() > MAX_KEY_SIZE || value.size() > MAX_WIRE_VALUE_SIZE)
            throw std::length_error("KVCache key or value exceeds protocol limit");
        Header header{MAGIC, VERSION, static_cast<uint8_t>(command), static_cast<uint32_t>(key.size()),
                      static_cast<uint32_t>(value.size())};
        validate(header);
        std::vector<uint8_t> bytes(HEADER_SIZE + key.size() + value.size());
        auto magic = htons(MAGIC);
        auto key_size = htonl(header.key_len);
        auto value_size = htonl(header.value_len);
        std::memcpy(bytes.data(), &magic, 2);
        bytes[2] = VERSION;
        bytes[3] = header.command;
        std::memcpy(bytes.data() + 4, &key_size, 4);
        std::memcpy(bytes.data() + 8, &value_size, 4);
        std::memcpy(bytes.data() + HEADER_SIZE, key.data(), key.size());
        std::memcpy(bytes.data() + HEADER_SIZE + key.size(), value.data(), value.size());
        return bytes;
    }
    // Caller must supply at least HEADER_SIZE bytes; validate before allocating the body.
    static Header decodeHeader(const uint8_t* bytes) {
        Header h{};
        std::memcpy(&h.magic, bytes, 2);
        h.magic = ntohs(h.magic);
        h.version = bytes[2];
        h.command = bytes[3];
        std::memcpy(&h.key_len, bytes + 4, 4);
        h.key_len = ntohl(h.key_len);
        std::memcpy(&h.value_len, bytes + 8, 4);
        h.value_len = ntohl(h.value_len);
        return h;
    }
};
}  // namespace kvcache
