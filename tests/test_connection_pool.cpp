#include "kvrpc/connection_pool.h"
#include "test_support.h"
#include <atomic>
using namespace std::chrono_literals;

int main() {
    Throws([&] { kvrpc::ConnectionPool pool("127.0.0.1", 1, 0); });
    Throws([&] { kvrpc::ConnectionPool pool("bad", 1, 1); });
    TestServer server([](int fd) { char byte; while (recv(fd, &byte, 1, 0) > 0) {} });
    auto pool = std::make_unique<kvrpc::ConnectionPool>("127.0.0.1", server.port(), 1, kvrpc::TransportOptions{}, 100ms);
    auto lease = pool->Acquire();
    ErrorIs(kvrpc::ErrorCode::timeout, [&] { pool->Acquire(); });
    auto raw = lease.get(); lease.reset();
    lease = pool->Acquire(); CHECK(lease.get() == raw);
    auto waiting = std::async(std::launch::async, [&] { ErrorIs(kvrpc::ErrorCode::closed, [&] { pool->Acquire(); }); });
    pool->Close(); waiting.get();
    pool.reset(); CHECK(lease->IsConnected()); // Outstanding leases survive the wrapper.
    lease.reset(); server.Finish();

    TestServer concurrent([](int fd) { char byte; while (recv(fd, &byte, 1, 0) > 0) {} });
    kvrpc::ConnectionPool shared("127.0.0.1", concurrent.port(), 1);
    std::atomic<int> active{0};
    std::vector<std::future<void>> workers;
    for (int i = 0; i < 12; ++i) workers.push_back(std::async(std::launch::async, [&] {
        for (int j = 0; j < 25; ++j) {
            auto connection = shared.Acquire(); CHECK(active.fetch_add(1) == 0);
            std::this_thread::yield(); CHECK(active.fetch_sub(1) == 1);
        }
    }));
    for (auto& worker : workers) worker.get();
    shared.Close(); concurrent.Finish();
    // A bound, non-listening socket reserves a port that deterministically refuses connections.
    int reserved = socket(AF_INET, SOCK_STREAM, 0); CHECK(reserved >= 0);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(reserved, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    socklen_t address_size = sizeof(address);
    CHECK(getsockname(reserved, reinterpret_cast<sockaddr*>(&address), &address_size) == 0);
    kvrpc::ConnectionPool refused("127.0.0.1", ntohs(address.sin_port), 1, {100ms, 100ms}, 100ms);
    for (int i = 0; i < 3; ++i)
        ErrorIs(kvrpc::ErrorCode::connection, [&] { refused.Acquire(); });
    close(reserved);
}
