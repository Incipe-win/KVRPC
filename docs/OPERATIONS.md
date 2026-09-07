# Operations

Build with Linux, C++17, CMake, and the Protobuf compiler/development library. The Docker image builds the same CMake targets and runs as UID 10001 with the Protobuf runtime installed.

```sh
./build/release/kv_server PORT AOF_PATH BIND_ADDRESS group
```

The optional sync argument is `group` or `always`. Defaults are 8080, appendonly.aof, 127.0.0.1, and group. Configuration is read once at startup.

| Environment variable | Default | Meaning |
| --- | --- | --- |
| KVRPC_IO_THREADS | 2 | Connection-owning ET threads |
| KVRPC_CONNECTIONS | 4096 | Simultaneous accepted connections |
| KVRPC_QUEUE_CAPACITY | 1024 | Waiting storage jobs |
| KVRPC_QUEUE_BYTES | 67108864 | Waiting request bytes |
| KVRPC_OUTPUT_BYTES | 8388608 | Per-connection output hard limit |
| KVRPC_IDLE_MS | 30000 | Connection deadline between completed responses |
| KVRPC_CACHE_ENTRIES | 1000 | Aggregate entry capacity |
| KVRPC_CACHE_SHARDS | 16 | LRU capacity partitions |
| KVRPC_CACHE_BYTES | 67108864 | Aggregate accounted cache bytes |
| KVRPC_AOF_BYTES | 1073741824 | AOF size limit and rewrite policy input |

Shard byte budgets must fit the largest allowed entry, including metadata. Count/byte limits bound accounted objects, not exact RSS. Set OS fd limits for the intended connection count. Generic embeddings additionally configure business workers and per-connection in-flight limits through `ServerOptions`; the generic example accepts PORT IO_THREADS WORKERS.

Client defaults: four sockets via `ClientEndpoint`, 64 outstanding requests, 64 MiB serialized request bytes, 16 in-flight requests per RPC socket, 2 MiB payload, 2-second connect timeout and 5-second overall request timeout. KV allows one request per socket. Client destruction drains admitted calls; do not submit concurrently with destruction.

KV startup/shutdown output is JSON. `Stats()` returns cache hits/misses, accounted bytes, evictions, AOF records/syncs/bytes/rewrites/sync microseconds, active connections, queue count/bytes, rejections, timeouts, and cumulative handler microseconds/errors. Inspect deltas; counters are process-local. Handler time includes storage waits and can overlap across generic RPC workers.

## Persistence and recovery

Use a local filesystem with working fsync, atomic rename, and flock semantics. The AOF and its `.lock` file belong to one server. Rewrite uses a temporary file in the same directory and synchronizes the replacement and directory. A stopped-server backup is the simplest consistent backup procedure; retain both the original file and configuration before upgrading. Do not replace or delete the live `.lock` file.

Automatic rewriting snapshots live entries when the log grows near its budget. A rewrite blocks KV command execution on its storage worker, while I/O reactors continue servicing sockets. If live data plus the next batch cannot fit, the batch is rejected; reads remain available. Increase the budget or reduce live data. An actual I/O/fsync error makes storage unavailable: fix the underlying issue before restart.

On checksum/truncation failure, preserve the damaged log and restore a verified backup. This implementation does not automatically truncate crash tails. It tests process death; it does not simulate physical power loss. Replay preserves logged mutations and TTL deadlines, but may reconstruct a different LRU resident set because intervening reads/evictions are not logged.

SIGINT/SIGTERM wakes the acceptor and drains worker jobs before stopping I/O threads. Responses in flight may be lost. A stalled callback/filesystem operation can extend shutdown; configure the supervisor grace period from the deployment's storage behavior.

## Access and load

The library has no native authentication/TLS endpoint. Bind to loopback or a controlled private interface and use an authenticated transport boundary when remote access is needed. It has no replication, service discovery, or failover.

Use the network fixed-arrival-rate probe and persistence benchmark separately. Measure errors, offered/completed load, dropped admissions, P99, CPU, and RSS rather than throughput alone. Increase I/O threads only when measured networking work benefits; serialized KV storage does not become parallel by increasing reactor count.
