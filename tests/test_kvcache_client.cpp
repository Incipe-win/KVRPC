#include "kvrpc/kvcache_client.h"
#include "test_support.h"
#include <map>

kvcache::Message Request(int fd) {
    uint8_t bytes[kvcache::HEADER_SIZE]; Read(fd, reinterpret_cast<char*>(bytes), sizeof(bytes));
    auto h = kvcache::Message::decodeHeader(bytes); kvcache::Message::validate(h);
    kvcache::Message request{h, std::string(h.key_len, '\0'), std::string(h.value_len, '\0')};
    Read(fd, request.key.data(), request.key.size()); Read(fd, request.value.data(), request.value.size());
    return request;
}
int main() {
    TestServer server([](int fd) {
        std::map<std::string, std::string> values;
        for (int i = 0; i < 6; ++i) {
            auto request = Request(fd); std::string value;
            auto command = static_cast<kvcache::Command>(request.header.command);
            if (command == kvcache::Command::SET) values[request.key] = request.value;
            if (command == kvcache::Command::GET) value = values[request.key];
            if (command == kvcache::Command::DEL) values.erase(request.key);
            if (command == kvcache::Command::STATS) value = "Hits: 1, Misses: 1";
            auto response = kvcache::Message::encode(command, request.key, value);
            Write(fd, reinterpret_cast<const char*>(response.data()), response.size(), true);
        }
    });
    auto pool = std::make_shared<kvrpc::ConnectionPool>("127.0.0.1", server.port(), 1);
    std::future<std::string> last;
    {
        kvrpc::KVCacheClient client(pool);
        CHECK(client.Set("long:key", std::string("v\0x", 3)).get());
        CHECK(client.Get("long:key").get() == std::string("v\0x", 3));
        CHECK(client.Delete("long:key").get()); CHECK(client.Get("long:key").get().empty());
        CHECK(client.Stats().get() == "Hits: 1, Misses: 1");
        last = client.Get("missing");
        ErrorIs(kvrpc::ErrorCode::invalid_argument, [&] { client.Set("k", std::string(kvcache::MAX_VALUE_SIZE + 1, 'v')); });
    }
    CHECK(last.get().empty()); server.Finish();
    TestServer no_ack([](int fd) { Request(fd); });
    kvrpc::KVCacheClient unconfirmed(std::make_shared<kvrpc::ConnectionPool>("127.0.0.1", no_ack.port(), 1));
    ErrorIs(kvrpc::ErrorCode::transport, [&] { unconfirmed.Set("k", "v").get(); }); no_ack.Finish();
    // A malformed response closes its socket; the next operation reconnects safely.
    int calls = 0;
    TestServer bad([&](int fd) {
        auto request = Request(fd);
        auto response = kvcache::Message::encode(kvcache::Command::GET, calls++ == 0 ? "x" : request.key, "good");
        Write(fd, reinterpret_cast<const char*>(response.data()), response.size());
    }, 2);
    kvrpc::KVCacheClient client(std::make_shared<kvrpc::ConnectionPool>("127.0.0.1", bad.port(), 1));
    ErrorIs(kvrpc::ErrorCode::protocol, [&] { client.Get("k").get(); });
    CHECK(client.Get("k").get() == "good"); bad.Finish();
}
