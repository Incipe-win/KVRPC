# Changelog

## Unreleased

### RPC server and measured group commit

- Add public typed RpcServer registration/dispatch and structured remote errors; preserve success framing.
- Share the bounded poll transport between generic RPC and KVCache; revisit buffered pipelines without the poll delay.
- Add ready-request group commit, per-mutation baseline mode, AOF counters, and batch ordering/failure tests.
- Add a read-through profile application and a reproducible C++ network/persistence benchmark with raw A/B trials.
- Test both sync modes through process death and explicitly test read-driven LRU replay differences.

### Correctness and reliability

- Wait for complete KVCache mutation acknowledgements and validate echoed response keys.
- Replace sentinel error strings with typed exceptions and reject malformed or oversized responses.
- Add connect, pool acquisition, and whole-request I/O timeouts.
- Handle interrupted calls, partial transfers, peer closure, and SIGPIPE safely.
- Keep connection leases and admitted asynchronous work safe across wrapper destruction.
- Replace one-thread-per-call execution with bounded workers and explicit queue rejection.
- Define portable scalar byte order while preserving the original little-endian wire representation.
- Implement cache deletion and exact aggregate shard capacity limits.
- Replace incomplete server writes with bounded nonblocking input/output handling.
- Synchronize AOF mutations before acknowledgement; enforce exclusive log ownership and strict replay validation.
- Add controlled shutdown and fail-closed behavior after storage errors.

### Build and verification

- Make failed-connection recovery tests portable across TCP stacks, verify successful reuse afterward, and report expected/actual error codes on assertion failures.
- Add CMake builds, CTest registration, installation, and an exported `KVRPC::kvrpc` package target.
- Correct Xmake include paths and expose the shared protocol without a sibling checkout.
- Replace demonstration programs with automatically checked tests and real process integration.
- Add CI configurations for Release, Debug, sanitizers, package consumption, and containers.
- Remove tracked editor indexes, generated compile commands, and obsolete submodule metadata.
- Rewrite Markdown documentation in English with explicit protocol and operational contracts.

### Migration notes

- `ConnectionPool::Acquire()` now throws if acquisition or connection establishment fails.
- Client methods surface execution failures through futures; local validation can throw before returning a future.
- Clients are noncopyable and drain admitted work on destruction.
- `Set` no longer reports success solely because request bytes were sent.
- Version-1 empty-value/missing-key ambiguity is retained for wire compatibility.
- The server defaults to loopback. Docker builds now use the repository root as their context.
- The bundled server uses a serial `poll` event loop and synchronous persistence. Earlier epoll/worker-pool performance claims do not describe this implementation.
- AOF flush-interval behavior was removed. The optional logger constructor parameter now specifies the maximum log size in bytes.
