#include "kvrpc/connection_pool.h"
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>

using namespace kvrpc;

void test_pool_concurrency() {
    // We create a mocked pool with 2 "slots"
    // Since we are not actually running a server on port 9999,
    // the Connect() inside pool will fail and IsConnected() == false.
    // That's fine, we are just testing the pool's thread synchronization mechanisms
    // and custom deleter here.
    ConnectionPool pool("127.0.0.1", 9999, 2);

    auto worker = [&pool](int id) {
        std::cout << "Thread " << id << " waiting for connection..." << std::endl;
        auto conn = pool.Acquire();
        std::cout << "Thread " << id << " acquired connection (connected=" << conn->IsConnected() << ")" << std::endl;

        // Simulate some work taking time...
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        std::cout << "Thread " << id << " releasing connection..." << std::endl;
        // conn goes out of scope and custom deleter is called here!
    };

    // We have 2 pool slots, but spawn 4 threads!
    // Threads 3 and 4 should block and successfully wait for 1 and 2 to free their connections.
    std::vector<std::thread> threads;
    for (int i = 1; i <= 4; ++i) {
        threads.emplace_back(worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "All Connection Pool concurrency tests passed!" << std::endl;
}

int main() {
    test_pool_concurrency();
    return 0;
}
