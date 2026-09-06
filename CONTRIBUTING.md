# Contributing

## Development build

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/debug --parallel 4
ctest --test-dir build/debug --output-on-failure --parallel 4
```

Point your editor at `build/debug/compile_commands.json`. Generated build files, editor indexes, and AOF files should not be committed.

## Sanitizers

Run AddressSanitizer and UndefinedBehaviorSanitizer together:

```sh
cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++ -DKVRPC_SANITIZE=ON
cmake --build build/asan --parallel 4
ctest --test-dir build/asan --output-on-failure --parallel 4
```

Run ThreadSanitizer in a separate build:

```sh
cmake -S . -B build/tsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DKVRPC_TSAN=ON
cmake --build build/tsan --parallel 4
ctest --test-dir build/tsan --output-on-failure --parallel 2
```

Sanitizer runtime support depends on the compiler and host kernel. Report startup/runtime incompatibilities separately from test failures; do not suppress race or memory diagnostics to obtain a passing build.

## Test coverage

| Test | Contract |
| --- | --- |
| `serializer` | Byte layout, binary strings, bounds, invalid encodings |
| `connection_pool` | Exclusive leasing, contention, timeout, shutdown, surviving leases |
| `rpc_client` | Framing, connection reuse, response validation, future lifetime |
| `kvcache_client` | Acknowledgements, echoed keys, commands, reconnect after invalid responses |
| `transport` | Partial transfers, deadlines, EOF, reset handling |
| `executor` | Bounded admission, queue rejection, drain, exception propagation |
| `storage` | LRU behavior, deletion, log locking, replay, corruption, fail-closed limits |
| `server_integration` | Built server and C++ example, large frames, pipelining, concurrency, crash recovery, signals |

Assertions remain active in Release builds. Network fixtures bind loopback ports before launching client work and propagate server-side failures back to the test. Tests do not depend on a manually started server.

## Installed package check

```sh
cmake --install build/release --prefix /tmp/kvrpc-install
cmake -S tests/install -B build/consumer -DCMAKE_PREFIX_PATH=/tmp/kvrpc-install
cmake --build build/consumer
./build/consumer/consumer
```

## Change requirements

Keep public headers self-contained and maintain the C++17 client baseline. For protocol changes, update the shared definition and protocol reference together, and include compatibility tests using explicit expected bytes. For networking and lifecycle changes, cover the failure path as well as successful requests.

Use the repository's `.clang-format` style when a formatter is available. Keep public documentation in English and describe measured behavior. Performance claims need a reproducible workload, toolchain, hardware specification, and recorded result.
