#include <kvrpc/kvcache_client.h>
#include <kvrpc/rpc_client.h>
#include <kvrpc/rpc_server.h>
#include <fstream>
#include <iostream>
#include <thread>

// Synthetic read-through example. Source revision namespaces immutable profile snapshots.
int main(int argc, char** argv) {
    if (argc != 4) { std::cerr << "Usage: profile_demo kv_port profiles.tsv source_revision\n"; return 1; }
    kvrpc::RpcServer service(0);
    std::future<void> serving;
    try {
        auto pool = std::make_shared<kvrpc::ConnectionPool>("127.0.0.1", std::stoi(argv[1]), 2);
        kvrpc::KVCacheClient cache(pool);
        std::string source = argv[2], revision = argv[3];
        uint64_t source_reads = 0;
        auto key = [revision](const std::string& id) { return "profile:" + revision + ":" + id; };
        service.Register<std::string, std::string>("profile.get", [&](const std::string& id) {
            try {
                auto cached = cache.Get(key(id)).get();
                if (!cached.empty()) return cached;
            } catch (const kvrpc::Error&) { /* Cache outage: consult the authoritative source. */ }
            ++source_reads;
            std::ifstream file(source);
            if (!file) throw std::runtime_error("Profile source unavailable");
            std::string line;
            while (std::getline(file, line)) {
                auto tab = line.find('\t');
                if (tab == std::string::npos || line.substr(0, tab) != id) continue;
                auto profile = line.substr(tab + 1);
                if (profile.empty()) throw std::runtime_error("Empty source profile");
                try { cache.Set(key(id), profile).get(); } catch (const kvrpc::Error&) {}
                return profile;
            }
            throw std::runtime_error("Profile not found");
        });
        service.Register<void, std::string>("profile.invalidate", [&](const std::string& id) { cache.Delete(key(id)).get(); });
        service.Register<uint64_t>("profile.source_reads", [&] { return source_reads; });
        serving = std::async(std::launch::async, [&] { service.Start(); });
        while (!service.Port()) {
            if (serving.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready) serving.get();
        }
        try {
            kvrpc::RpcClient client(std::make_shared<kvrpc::ConnectionPool>("127.0.0.1", service.Port(), 2));
            client.Call<void>("profile.invalidate", "42").get();
            auto first = client.Call<std::string>("profile.get", "42").get();
            auto second = client.Call<std::string>("profile.get", "42").get();
            if (first != second || client.Call<uint64_t>("profile.source_reads").get() != 1)
                throw std::runtime_error("Read-through cache verification failed");
            client.Call<void>("profile.invalidate", "42").get();
            auto third = client.Call<std::string>("profile.get", "42").get();
            if (third != first || client.Call<uint64_t>("profile.source_reads").get() != 2)
                throw std::runtime_error("Cache invalidation verification failed");
            std::cout << "Profile: " << first << "\n3 RPC lookups, 2 source reads; cache hit and invalidation verified\n";
        } catch (...) { service.Stop(); serving.wait(); throw; }
        service.Stop(); serving.get();
    } catch (const std::exception& error) {
        service.Stop(); if (serving.valid()) serving.wait();
        std::cerr << error.what() << '\n'; return 1;
    }
}
