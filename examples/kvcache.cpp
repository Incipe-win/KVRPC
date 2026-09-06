#include "kvrpc/kvcache_client.h"
#include <iostream>

int main(int argc, char** argv) {
    try {
        int port = argc > 1 ? std::stoi(argv[1]) : 8080;
        if (port < 1 || port > 65535) throw std::invalid_argument("Invalid port");
        auto pool = std::make_shared<kvrpc::ConnectionPool>("127.0.0.1", static_cast<uint16_t>(port), 4);
        kvrpc::KVCacheClient client(pool);
        client.Set("example:key", "hello").get();
        auto value = client.Get("example:key").get();
        if (value != "hello") throw std::runtime_error("Unexpected value");
        std::cout << value << '\n' << client.Stats().get() << '\n';
        client.Delete("example:key").get();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
