#include "cache_service.h"
#include "test_support.h"
#include <fstream>

class TempFile {
public:
    TempFile() { char name[] = "/tmp/kvrpc-aof-XXXXXX"; int fd = mkstemp(name); CHECK(fd >= 0); close(fd); path = name; }
    ~TempFile() { unlink(path.c_str()); }
    std::string path;
};
std::string Call(kvcache::CacheService& service, kvcache::Command cmd, const std::string& key, const std::string& value = "") {
    auto request = kvcache::Message::encode(cmd, key, value); size_t consumed = 0;
    auto response = service.Handle(request, consumed); CHECK(consumed == request.size());
    auto h = kvcache::Message::decodeHeader(response.data());
    return std::string(reinterpret_cast<const char*>(response.data() + kvcache::HEADER_SIZE + h.key_len), h.value_len);
}
int main() {
    using kvcache::Command;
    Throws([&] { kvcache::LRUCache<int, int> cache(0); });
    Throws([&] { kvcache::ShardedCache<int, int> cache(1, 0); });
    kvcache::LRUCache<int, int> lru(2); lru.put(1, 1); lru.put(2, 2); CHECK(lru.get(1) == 1);
    lru.put(3, 3); CHECK(!lru.get(2)); CHECK(lru.remove(1)); CHECK(!lru.remove(1));
    kvcache::ShardedCache<int, int> shards(3, 16);
    for (int i = 0; i < 100; ++i) shards.put(i, i);
    CHECK(shards.size() == 3);
    TempFile file;
    {
        kvcache::CacheService service(10, 2, file.path);
        Throws([&] { kvcache::AofLogger competing(file.path); });
        Call(service, Command::SET, "keep", std::string("a\0b", 3));
        Call(service, Command::SET, "gone", "value"); Call(service, Command::DEL, "gone");
        CHECK(Call(service, Command::GET, "keep") == std::string("a\0b", 3));
        CHECK(Call(service, Command::GET, "gone").empty());
        auto malformed = kvcache::Message::encode(Command::GET, "keep", "invalid"); size_t used = 123;
        Throws([&] { service.Handle(malformed, used); }); CHECK(used == 0);
        malformed = {0xca}; CHECK(service.Handle(malformed, used).empty() && used == 0);
    }
    {
        kvcache::CacheService restored(10, 2, file.path);
        CHECK(Call(restored, Command::GET, "keep") == std::string("a\0b", 3));
        CHECK(Call(restored, Command::GET, "gone").empty());
    }
    {
        std::ofstream corrupt(file.path, std::ios::binary | std::ios::app); corrupt.put('x');
    }
    Throws([&] { kvcache::CacheService corrupted(10, 2, file.path); });
    TempFile partial;
    {
        auto data = kvcache::Message::encode(Command::SET, "key", "value");
        std::ofstream out(partial.path, std::ios::binary); out.write(reinterpret_cast<const char*>(data.data()), data.size() - 1);
    }
    int callbacks = 0;
    kvcache::AofLogger log(partial.path);
    Throws([&] { log.replay([&](Command, const std::string&, const std::string&) { ++callbacks; }); }); CHECK(callbacks == 0);
    TempFile limited;
    kvcache::CacheService service(2, 1, limited.path, kvcache::HEADER_SIZE + 2);
    Call(service, Command::SET, "k", "v");
    Throws([&] { Call(service, Command::SET, "k", "w"); });
    Throws([&] { Call(service, Command::GET, "k"); }); // Storage failure fails closed.
}
