#include "kvrpc/serializer.h"
#include "kvrpc/kv_protocol.h"
#include "test_support.h"
#include <limits>

int main() {
    kvrpc::Serializer s;
    s.Serialize(uint32_t(0x12345678), int32_t(-42), std::string("a\0b", 3), 3.14159, true);
    CHECK(static_cast<unsigned char>(s.GetBuffer()[0]) == 0x78);
    kvrpc::Serializer decoded(s.GetBuffer());
    uint32_t a; int32_t b; std::string c; double d; bool e;
    decoded.Deserialize(a, b, c, d, e);
    CHECK(a == 0x12345678 && b == -42 && c == std::string("a\0b", 3) && d == 3.14159 && e);
    CHECK(decoded.Remaining() == 0);
    Throws<std::out_of_range>([&] { decoded.Deserialize(a); });
    kvrpc::Serializer invalid(std::vector<char>{2});
    Throws<std::invalid_argument>([&] { invalid.Deserialize(e); });
    kvrpc::Serializer truncated(std::vector<char>{char(255), char(255), char(255), char(255)});
    Throws<std::out_of_range>([&] { truncated.Deserialize(c); });
    Throws<std::invalid_argument>([&] { s.Serialize(static_cast<const char*>(nullptr)); });
    s.Reset(); s.Serialize(); s.Deserialize(); CHECK(s.Remaining() == 0);
    s.Serialize(std::string(), std::numeric_limits<int64_t>::min(), false);
    int64_t lowest; s.Deserialize(c, lowest, e);
    CHECK(c.empty() && lowest == std::numeric_limits<int64_t>::min() && !e);
    kvrpc::Serializer limited(4);
    Throws<std::length_error>([&] { limited.Serialize(std::string("x")); });
    CHECK(limited.GetBuffer().empty());
    limited.Serialize(uint32_t(1));
    Throws<std::length_error>([&] { limited.Serialize(uint8_t(1)); });
    auto kv = kvcache::Message::encode(kvcache::Command::SET, "k", "v");
    CHECK(kv == std::vector<uint8_t>({0xca, 0xfe, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 'k', 'v'}));
    auto h = kvcache::Message::decodeHeader(kv.data()); kvcache::Message::validate(h);
    h.version = 2; Throws([&] { kvcache::Message::validate(h); });
}
