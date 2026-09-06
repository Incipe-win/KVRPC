# KVCache

KVCache is the C++ cache and TCP server bundled with KVRPC. It provides sharded LRU storage, binary `SET`/`GET`/`DEL`/`STATS` commands, and append-only persistence. This directory is a vendored source snapshot, not a Git submodule.

## Build

Use the repository-level build so the server and client share the same protocol definition:

```sh
cmake -S .. -B ../build/release -DCMAKE_BUILD_TYPE=Release
cmake --build ../build/release --parallel 4
ctest --test-dir ../build/release --output-on-failure --parallel 4
```

These commands assume the current directory is `KVCache`. CMake requires no external testing libraries for the maintained suite.

## Run

```sh
mkdir -p ../build/data
../build/release/kv_server 8080 ../build/data/appendonly.aof 127.0.0.1
```

The server runs in the foreground and handles `SIGINT`/`SIGTERM`. Mutation acknowledgements follow successful append and synchronization. Replay rejects invalid or partial records, and another process cannot open the active log for writing.

## Design and limits

The server handles TCP peers with a bounded nonblocking `poll` loop. Handlers execute serially, including persistence. Cache access is mutex-protected, and the total entry capacity is divided across shards. Per-shard LRU eviction can occur before the aggregate capacity is fully used.

The executable defaults to 1,000 cache entries, 16 shards, 128 peers, and a 1 GiB AOF limit. Keys are limited to 64 KiB and values to 1 MiB. This is a single-node cache: replication, TTL, HTTP, authentication, TLS, and log compaction are not implemented.

See the repository [operations guide](../docs/OPERATIONS.md) for memory planning, backup procedures, recovery constraints, and container commands. The [protocol reference](../docs/PROTOCOL.md) specifies exact wire behavior, including the distinction the version-1 protocol cannot make between empty and missing values.

## Historical microbenchmarks

The cache-only tests and benchmark under `tests/` use GoogleTest and Google Benchmark through this directory's Xmake configuration:

```sh
xmake
xmake run test_lru_cache
xmake run test_sharded_cache
xmake run benchmark_cache
```

This optional path may download its test dependencies. Cache microbenchmarks measure in-memory operations, not server or persistence throughput. Earlier benchmark numbers have been removed because they do not establish performance for the current implementation. Record fresh measurements with their hardware, compiler, workload, and revision before making performance claims.

## License

The bundled KVCache code retains its [MIT license](LICENSE).
