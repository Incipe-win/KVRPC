# KVRPC

KVRPC is a C++17 TCP client library with asynchronous RPC calls, reusable connections, bounded work queues, and explicit failure handling. The repository also contains a Linux KVCache server for end-to-end development and deployment in a controlled network.

The client library has no third-party runtime dependencies. It supports a generic length-prefixed RPC format and the KVCache binary protocol. These are separate protocols: the KVCache server accepts KVCache commands only.

## Capabilities

- Fixed-size worker pools return `std::future` results and reject excess queued work.
- TCP operations handle partial transfers, interrupted system calls, disconnects, and deadlines.
- Connection leases remain valid after their pool wrapper is destroyed; failed connections are discarded before reuse.
- Response headers, lengths, commands, keys, and decoded payloads are validated before results are returned.
- KVCache supports `SET`, `GET`, `DEL`, and `STATS`, with binary-safe keys and values.
- The server acknowledges mutations after appending and synchronizing its log, and rejects corrupt logs at startup.
- Automated tests cover protocol compatibility, concurrency, overload, failure handling, persistence, and process lifecycle.

## Build and test

Requirements: a C++17 compiler, CMake 3.16 or newer, and POSIX threads. Linux builds include the server; macOS builds provide the client library. Python 3 is required for the Linux integration test. No dependency downloads are required for the CMake build.

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel 4
ctest --test-dir build/release --output-on-failure --parallel 4
```

Tests create local TCP listeners on dynamically allocated loopback ports. They start their own servers and use temporary persistence files. A sandbox that prohibits sockets must grant local-network test access.

To build only the library and example:

```sh
cmake -S . -B build/client -DCMAKE_BUILD_TYPE=Release \
  -DKVRPC_BUILD_SERVER=OFF -DBUILD_TESTING=OFF
cmake --build build/client --parallel 4
```

The root [Xmake configuration](xmake.lua) is also maintained:

```sh
xmake f -m release
xmake
xmake test
```

CMake/CTest is the primary verification path. The historical cache benchmarks under `KVCache/tests` use Google Benchmark and GoogleTest through the separate KVCache Xmake build.

## Run the server and example

From the repository root, start the server in one terminal:

```sh
mkdir -p build/data
./build/release/kv_server 8080 build/data/appendonly.aof 127.0.0.1
```

Run the C++ example in another terminal:

```sh
./build/release/kvrpc_example 8080
```

`SIGINT` and `SIGTERM` stop the listener and close its client connections. The server defaults to loopback and stores its append-only log at the supplied path.

## Use the client

```cpp
#include <kvrpc/kvcache_client.h>
#include <chrono>
#include <iostream>
#include <memory>

int main() {
    using namespace std::chrono_literals;
    auto pool = std::make_shared<kvrpc::ConnectionPool>(
        "127.0.0.1", 8080, 4,
        kvrpc::TransportOptions{2s, 5s}, 5s);
    kvrpc::KVCacheClient client(pool);

    try {
        client.Set("user:42:name", "Ada").get();
        auto pending = client.Get("user:42:name");
        std::cout << pending.get() << '\n';
        client.Delete("user:42:name").get();
    } catch (const kvrpc::Error& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

Wait for a successful `Set` before issuing a dependent `Get`. Concurrent calls can execute on different connections and have no submission-order guarantee. `Set` and `Delete` return `true` after a valid acknowledgement; failures throw. Version 1 returns an empty string for both a missing key and a stored empty value.

`RpcClient::Call<T>(method, args...)` uses the generic protocol described in [Protocol reference](docs/PROTOCOL.md). Applications must supply a compatible generic RPC server. Use fixed-width integer types in RPC signatures.

### Resource limits and lifetime

| Setting | Default | Configuration |
| --- | --- | --- |
| Connection slots | Required constructor argument | `ConnectionPool`, 1–4,096 |
| Connection timeout | 2 seconds | `TransportOptions::connect_timeout` |
| Pool acquisition timeout | 5 seconds | `ConnectionPool` constructor |
| Total socket I/O deadline per request | 5 seconds | `TransportOptions::io_timeout` |
| Worker threads per client | 4 | `ClientOptions::workers`, 1–256 |
| Waiting tasks per client | 64 | `ClientOptions::queue_capacity`, 1–65,536 |
| Generic RPC payload limit | 2 MiB | `ClientOptions::max_frame_bytes`, up to 64 MiB |
| KVCache key / value limit | 64 KiB / 1 MiB | Protocol constants |

Pool acquisition, connection establishment, and request I/O have separate budgets. Time spent waiting in the client queue is additional. Request I/O uses one monotonic deadline across the complete send/receive exchange, so incremental traffic does not extend it.

Clients accept concurrent calls. Destruction drains admitted work and joins workers; it can therefore wait for queued requests and their timeouts. Stop submitting calls before destroying a client or pool. A lease can outlive its pool, and an already-submitted future can outlive its client. Connections themselves require exclusive use. Explicit `ConnectionPool::Close()` wakes acquisition waiters and prevents new leases, while existing leases finish normally.

Queue overload and execution errors are delivered through futures. Invalid configuration and locally detected oversized requests fail synchronously. Always observe futures. Requests are never retried automatically because a transport failure after sending a mutation has an ambiguous outcome.

## Install and consume

```sh
cmake --install build/release --prefix "$HOME/.local"
```

In a consuming project:

```cmake
find_package(KVRPC 1 REQUIRED CONFIG)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE KVRPC::kvrpc)
```

Pass `-DCMAKE_PREFIX_PATH="$HOME/.local"` when configuring the consuming project. Public headers include the KVCache protocol; consumers do not need a sibling KVCache checkout.

## Deployment scope

The server is a single-node, entry-bounded LRU cache with synchronous append-only persistence. It has no replication, clustering, authentication, TLS, TTL, HTTP endpoint, automatic log compaction, or generic RPC dispatch. Deploy it on loopback or an access-controlled private network; use an authenticated transport boundary where remote access is required.

The repository supplies tested correctness and resource controls, not a workload-independent production certification. Capacity, latency, recovery procedures, filesystem behavior, and network controls must be validated for the deployment. See [Operations](docs/OPERATIONS.md) for concrete limits and recovery procedures.

## Documentation

- [Architecture](docs/ARCHITECTURE.md): ownership, concurrency, and failure behavior.
- [Protocol reference](docs/PROTOCOL.md): byte layouts and compatibility constraints.
- [Operations](docs/OPERATIONS.md): deployment, persistence, monitoring, and recovery.
- [Contributing](CONTRIBUTING.md): development and verification commands.
- [Validation record](docs/VALIDATION.md): local test results and unverified scope.
- [Changes](CHANGELOG.md): behavior changes from the original prototype.
- [KVCache](KVCache/README.md): bundled server and cache components.
