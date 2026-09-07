#include <fstream>

#include "cache_service.h"
#include "test_support.h"
using namespace std::chrono_literals;
struct File {
    std::string path;
    File() {
        char p[] = "/tmp/kvrpc-v2-XXXXXX";
        int fd = mkstemp(p);
        CHECK(fd >= 0);
        close(fd);
        path = p;
    }
    ~File() {
        unlink(path.c_str());
        unlink((path + ".lock").c_str());
    }
};
std::string Call(kvcache::CacheService& service, kvcache::Command cmd, std::string key, std::string value = {}) {
    auto request = kvcache::Message::encode(cmd, key, value);
    size_t n = 0;
    auto reply = service.Handle(request, n);
    CHECK(n == request.size());
    auto header = kvcache::Message::decodeHeader(reply.data());
    return {reinterpret_cast<char*>(reply.data() + kvcache::HEADER_SIZE + header.key_len), header.value_len};
}
int main() {
    using kvcache::Command;
    File file;
    {
        kvcache::CacheService service(100, 1, file.path, 4096);
        Call(service, Command::SET, "empty", "");
        kvrpc::wire::CacheValue result;
        CHECK(result.ParseFromString(Call(service, Command::LOOKUP, "empty")) && result.found());
        CHECK(result.ParseFromString(Call(service, Command::LOOKUP, "missing")) && !result.found());
        kvrpc::wire::CacheValue ttl;
        ttl.set_value("ttl");
        ttl.set_ttl_ms(1000);
        Call(service, Command::SET_TTL, "expiring", ttl.SerializeAsString());
        for (int i = 0; i < 200; ++i) Call(service, Command::SET, "hot", std::string(64, 'a' + i % 20));
        CHECK(Call(service, Command::STATS, "").find("AOF rewrites: 0") == std::string::npos);
        service.Compact();
    }
    {
        kvcache::CacheService service(100, 1, file.path, 4096);
        CHECK(Call(service, Command::GET, "hot").size() == 64);
        std::this_thread::sleep_for(1100ms);
        CHECK(Call(service, Command::GET, "expiring").empty());
    }
    // A payload bit flip with otherwise valid framing is detected by CRC.
    {
        std::fstream data(file.path, std::ios::binary | std::ios::in | std::ios::out);
        data.seekg(-1, std::ios::end);
        char byte;
        data.get(byte);
        byte ^= 1;
        data.seekp(-1, std::ios::end);
        data.put(byte);
    }
    Throws([&] { kvcache::CacheService corrupt(100, 1, file.path, 4096); });
    kvcache::LRUCache<std::string, std::string> cache(100, 400);
    cache.put("a", std::string(200, 'a'));
    cache.put("b", std::string(200, 'b'));
    CHECK(!cache.get("a") && cache.get("b") && cache.getStats().bytes <= 400);
}
