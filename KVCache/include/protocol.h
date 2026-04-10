#pragma once

#include <arpa/inet.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace kvcache {

// Magic: 0xCAFE
const uint16_t MAGIC = 0xCAFE;
const uint8_t VERSION = 1;

enum class Command : uint8_t { SET = 1, GET = 2, DEL = 3, STATS = 4, UNKNOWN = 0 };

#pragma pack(push, 1)
struct Header {
    uint16_t magic;
    uint8_t version;
    uint8_t command;
    uint32_t key_len;
    uint32_t value_len;
};
#pragma pack(pop)

const size_t HEADER_SIZE = sizeof(Header);

struct Message {
    Header header;
    std::string key;
    std::string value;

    static std::vector<uint8_t> encode(Command cmd, const std::string& key, const std::string& value = "") {
        std::vector<uint8_t> buffer;
        buffer.resize(HEADER_SIZE + key.size() + value.size());

        Header* h = reinterpret_cast<Header*>(buffer.data());
        h->magic = htons(MAGIC);
        h->version = VERSION;
        h->command = static_cast<uint8_t>(cmd);
        h->key_len = htonl(key.size());
        h->value_len = htonl(value.size());

        std::memcpy(buffer.data() + HEADER_SIZE, key.data(), key.size());
        if (!value.empty()) {
            std::memcpy(buffer.data() + HEADER_SIZE + key.size(), value.data(), value.size());
        }

        return buffer;
    }

    // Helper to decode header from network byte order
    static Header decodeHeader(const uint8_t* data) {
        Header h;
        std::memcpy(&h, data, HEADER_SIZE);
        h.magic = ntohs(h.magic);
        h.key_len = ntohl(h.key_len);
        h.value_len = ntohl(h.value_len);
        return h;
    }
};

}  // namespace kvcache
