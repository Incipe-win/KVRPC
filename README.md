# KVRPC 2

A Linux C++17 RPC framework and persistent KV cache built around **epoll ET, Reactor threads, asynchronous clients, and Protocol Buffers**.

The acceptor distributes connections across I/O reactors. Each socket has one owning thread; bounded business workers execute RPC callbacks, while a dedicated storage worker serializes KV operations and group commit. Network threads never call business handlers or fsync.

## Build

```sh
sudo apt-get install cmake g++ protobuf-compiler libprotobuf-dev
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel 4
ctest --test-dir build/release --output-on-failure --parallel 4
```

Linux and Protobuf are required. Python 3 runs process integration tests. CMake/CTest is the supported verification path; `xmake.lua` provides an alternative build with `protobuf-cpp`.

## RPC

```cpp
#include <kvrpc/rpc_server.h>
#include <kvrpc/rpc_client.h>

// Server: register callbacks before Start(). Callbacks may run concurrently.
kvrpc::RpcServer server(8081);
server.Register<int64_t, int32_t, int32_t>("add",
    [](int32_t a, int32_t b) { return int64_t(a) + b; });
server.Start(); // Run on the server thread; Stop() wakes its reactor.

// Client in another process/thread:
kvrpc::RpcClient client(kvrpc::ClientEndpoint{"127.0.0.1", 8081, 2});
auto answer = client.Call<int64_t>("add", int32_t(20), int32_t(22)).get();
```

Each client has one ET thread serving its persistent connections. Request IDs allow multiple outstanding calls on the same connection and out-of-order responses. `future` completion does not occupy a worker while waiting for the network. Scalar types, strings/binary strings, void returns, and generated Protobuf messages are supported. The schema is [proto/rpc.proto](proto/rpc.proto).

Request admission is bounded by count and serialized bytes. The deadline begins at submission and includes queueing, connection establishment, and I/O. Requests are not retried automatically. Client destruction stops admission and drains admitted requests until completion/deadline.

`RpcClient(shared_ptr<ConnectionPool>)` remains a source adapter: it copies endpoint/slot/timeout settings into its own asynchronous transport. It does not acquire pool leases; closing that legacy pool does not close the asynchronous client. New code should use `ClientEndpoint` directly. `ConnectionPool` and `TcpConnection` remain available for synchronous integrations.

## KV cache

```sh
mkdir -p build/data
./build/release/kv_server 8080 build/data/appendonly.aof 127.0.0.1 group
./build/release/kvrpc_example 8080
```

```cpp
#include <kvrpc/kvcache_client.h>
using namespace std::chrono_literals;
kvrpc::KVCacheClient cache(kvrpc::ClientEndpoint{"127.0.0.1", 8080, 4});
cache.SetWithTTL("session:42", "value", 30s).get();
auto value = cache.Lookup("session:42").get(); // optional<string>: missing differs from empty
```

SET/DEL acknowledgements follow fsync. A storage thread groups queued mutations without an artificial batching delay; `always` uses one mutation sync at a time. TTL expiration is persisted as an absolute Unix timestamp. Sharded LRU has entry and accounted-byte budgets. AOF records have CRC32 checksums; automatic rewrite snapshots live entries to a synced replacement and atomically renames it. Capacity rejection leaves reads available; actual I/O failures stop further storage service.

The original GET API remains compatible and returns an empty string for either missing or empty values; use `Lookup` for unambiguous results. KV connections preserve request order. Generic RPC callbacks may complete out of order, so await dependent operations explicitly.

## Configuration and observability

KV configuration uses environment variables, including `KVRPC_IO_THREADS`, `KVRPC_CONNECTIONS`, `KVRPC_QUEUE_CAPACITY`, `KVRPC_QUEUE_BYTES`, `KVRPC_OUTPUT_BYTES`, `KVRPC_IDLE_MS`, `KVRPC_CACHE_ENTRIES`, `KVRPC_CACHE_SHARDS`, `KVRPC_CACHE_BYTES`, and `KVRPC_AOF_BYTES`. See [Operations](docs/OPERATIONS.md) for defaults.

RPC embeddings configure `ServerOptions`. The example accepts `kvrpc_rpc_server PORT IO_THREADS WORKERS`. `TcpServer::stats()`/`RpcServer::Stats()` expose active connections, admissions/rejections, completions, timeouts, queue size/bytes, and handler time/errors. KV STATS additionally exposes cache and persistence counters. KV startup/shutdown records are JSON.

## Install

```sh
cmake --install build/release --prefix /tmp/kvrpc-install
cmake -S tests/install -B build/consumer -DCMAKE_PREFIX_PATH=/tmp/kvrpc-install
cmake --build build/consumer
./build/consumer/consumer
```

Consumers use `find_package(KVRPC 2 REQUIRED CONFIG)` and link `KVRPC::kvrpc`. Generated Protobuf headers and the schema are installed with the library.

## Verify and measure

```sh
python3 benchmarks/network.py --build build/release --output benchmarks/network-results.json
python3 benchmarks/run.py --build build/release --seconds 2 --repeats 3 --output benchmarks/results-v2.json
```

The network probe measures fixed scheduled arrival rates, idle connections, I/O thread counts, latency, CPU, and RSS. The persistence harness compares identical workloads under always/group fsync policies. Neither short experiment establishes deployment capacity.

## Scope and migration

Version 2 intentionally changes generic RPC framing to `KVR2` + length + request ID + Protobuf. Version-1 generic RPC peers must upgrade together. Existing KV commands remain compatible. Legacy AOF records can be replayed; new appends and rewrites use checksummed records. Version-1 binaries cannot read upgraded logs; keep a backup for rollback.

This is a single-node cache. Read-driven LRU order between snapshots is not logged, so recovery may reconstruct a different resident key set. Keep an authoritative data source; TTL is not replication or a database transaction. Native TLS, authentication, clustering, and service discovery are outside this library's current scope; deploy behind the appropriate trusted network/transport boundary.

[Architecture](docs/ARCHITECTURE.md) · [Protocol](docs/PROTOCOL.md) · [Operations](docs/OPERATIONS.md) · [Validation](docs/VALIDATION.md) · [Performance](docs/PERFORMANCE.md) · [Use case](docs/USE_CASE.md)
