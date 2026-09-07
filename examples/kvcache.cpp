#include <iostream>
#include <thread>

#include "kvrpc/kvcache_client.h"

int main(int argc, char** argv) {
    try {
        int port = argc > 1 ? std::stoi(argv[1]) : 8080;
        if (port < 1 || port > 65535) throw std::invalid_argument("Invalid port");
        kvrpc::KVCacheClient client(kvrpc::ClientEndpoint{"127.0.0.1", static_cast<uint16_t>(port), 4});
        client.Set("example:key", "hello").get();
        auto value = client.Get("example:key").get();
        if (value != "hello") throw std::runtime_error("Unexpected value");
        std::cout << value << '\n' << client.Stats().get() << '\n';
        client.Delete("example:key").get();
        if (client.Lookup("example:key").get()) throw std::runtime_error("Missing key was found");
        client.Set("example:empty", "").get();
        auto empty = client.Lookup("example:empty").get();
        if (!empty || !empty->empty()) throw std::runtime_error("Empty value lookup failed");
        client.SetWithTTL("example:ttl", "temporary", std::chrono::milliseconds(20)).get();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        if (client.Lookup("example:ttl").get()) throw std::runtime_error("Expired key was found");
        client.Delete("example:empty").get();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
