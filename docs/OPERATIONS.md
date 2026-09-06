# Operations

## Deployment boundary

Run the bundled server as a single-node cache within a controlled network. Native TLS, authentication, authorization, replication, and failover are not implemented. The default listener is `127.0.0.1`. For remote clients, provide an authenticated network boundary and restrict access to the intended application identities.

The process accepts:

```text
kv_server [port=8080] [aof=appendonly.aof] [bind=127.0.0.1]
```

Use `kv_server --help` for the same usage information. Ports must be in `1`–`65535`; addresses must be numeric IPv4. Hostname resolution and IPv6 are not implemented.

## Resource planning

| Resource | Bundled executable default |
| --- | --- |
| Cache capacity | 1,000 entries across 16 shards |
| Concurrent accepted peers | 128 |
| Maximum key / value | 64 KiB / 1 MiB |
| Input and output frame limit per peer | 1,114,124 bytes each |
| Connection deadline | 30 seconds since acceptance or last completed response |
| AOF size limit | 1 GiB |
| Mutation durability | Append, then `fsync`, before acknowledgement |

Slow partial requests do not reset the connection deadline. Completed responses reset it. Excess peers are closed. Input/output frame limits bound retained application buffers, but allocator overhead and kernel socket buffers add memory usage.

Entry count is not a byte budget. At maximum key/value sizes, 1,000 entries alone can exceed 1 GiB before indexing and network buffers. Set process/container memory and descriptor limits from a measured workload. Changing server capacity, shards, peer count, or AOF limit currently requires constructing `CacheService`/`TcpServer` with different arguments in an embedding application or adjusting the executable defaults and rebuilding.

The event loop executes handlers serially, including disk synchronization. Measure p50/p95/p99 request latency, throughput, queue rejections, and memory under expected value sizes and concurrency. Do not use the historical cache microbenchmarks as network or persistence throughput measurements.

## Containers

Build from the repository root:

```sh
docker build -f KVCache/Dockerfile -t kvrpc-server:local .
docker volume create kvrpc-data
docker run --name kvrpc-server --detach \
  --publish 127.0.0.1:8080:8080 \
  --mount source=kvrpc-data,target=/data \
  --stop-timeout 30 \
  kvrpc-server:local
```

The image runs as UID `10001`. Its listener binds all interfaces inside the container; the example publishes only the host loopback interface. A bind-mounted data directory must be writable by UID `10001`. Persist `/data`; the container's writable layer is not a backup.

```sh
docker logs kvrpc-server
docker stop --time 30 kvrpc-server
```

The image is defined against the Ubuntu 24.04 tag. For a release pipeline, record the resolved base-image digest and resulting image digest along with the tested source revision.

## Persistence and recovery

Each accepted mutation is written to the append-only file and synchronized before the in-memory mutation and response. The parent directory is also synchronized when the log is opened. A second process cannot use the same log concurrently because the writer holds an exclusive advisory lock.

Acknowledged writes are covered by an abrupt-process-restart integration test. Actual power-loss durability also depends on the filesystem, storage device, and their synchronization guarantees. Reads and LRU ordering are not persisted. A recovered cache can evict different keys because replay contains mutations rather than read-access history.

The log has a 1 GiB default cap. Reaching the cap or encountering a write/sync failure makes the service reject further requests. Resolve the underlying condition and restart. Increasing the limit requires explicit configuration in an embedding application or a rebuilt executable; disk space alone does not override the configured cap. Automatic rotation, snapshots, and compaction are not implemented.

AOF records have framing validation but no checksums. Framing detects truncation and malformed metadata; arbitrary bit changes in otherwise valid payloads can remain undetected. Maintain verified backups on storage appropriate to the required recovery guarantees.

### Backups

1. Stop the server cleanly and verify process exit.
2. Copy the complete AOF to backup storage and record its checksum externally.
3. Restart against the original log.
4. Periodically test the backup by starting an isolated server and checking representative keys.

Copying an actively written AOF can capture a partial final record. The repository does not supply a consistent online-backup API.

### Startup failure

The server exits nonzero for a missing/unwritable parent directory, an incompatible file type, an unavailable log lock, excessive log size, or invalid/truncated records. It does not truncate or repair data automatically.

Preserve the failed file for investigation. Restore a verified backup, or perform a separately reviewed offline recovery that produces a new complete log. Do not append new records to a truncated file or discard the log without an explicit data-loss decision.

### Ambiguous mutation outcomes

A client can time out after the server commits but before its acknowledgement arrives. An unacknowledged mutation may appear after restart. Requests contain no operation IDs or deduplication state. Applications must reconcile ambiguous outcomes instead of assuming a transport exception means the write was rolled back.

## Monitoring and shutdown

Use `KVCacheClient::Stats()` as a protocol-level readiness check. It returns process-local hit/miss counts; it does not expose queue depth, disk usage, latency histograms, or per-client traffic. Collect application-side error-code counts and timings, plus operating-system memory, descriptor, and disk metrics.

Startup and request rejection messages go to standard output/error. Request rejection logging is limited to one message per second and omits keys and values. Configure log collection and retention at the process supervisor.

On `SIGINT` or `SIGTERM`, the current synchronous handler finishes and the event loop closes listeners and peers. Pending acknowledgements may be lost; clients must handle disconnects. The loop normally observes stop within its 100 ms poll interval, but a stalled filesystem call can extend shutdown. Set a supervisor grace period from measured worst-case storage behavior.

## Release validation

Before deploying a specific build:

- Pass Release, Debug, and sanitizer tests on the target platform.
- Verify the installed CMake package in a separate consuming project.
- Exercise the actual network boundary and persistent volume permissions.
- Measure latency and memory at expected load, including slow clients and storage pressure.
- Test backup restoration and process replacement with the selected storage configuration.

CI configuration provides Linux/macOS client builds, Linux server integration, sanitizer jobs, and a container smoke test. A workflow definition is not evidence that those jobs have run for a particular revision; record the actual results when releasing.
