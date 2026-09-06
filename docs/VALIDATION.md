# Validation Record

This record describes local verification of the working-tree implementation on September 6, 2026. It is not a deployment certification or evidence of a completed remote CI run.

## Environment

| Component | Version |
| --- | --- |
| Host | Linux x86_64, WSL2 kernel `6.18.33.2-microsoft-standard-WSL2` |
| GCC | 13.3.0 |
| Clang | 22.1.8 |
| CMake | 3.28.3 |
| Python | 3.12.3 |
| Docker Engine | 29.1.3 |

## Results

| Configuration or check | Result |
| --- | --- |
| GCC Debug, full CTest suite | 10/10 passed |
| GCC Release, full CTest suite | 10/10 passed |
| Clang AddressSanitizer + UndefinedBehaviorSanitizer | 10/10 passed |
| Clang ThreadSanitizer, including server integration | 10/10 passed |
| Release installation and independent CMake consumer | Built, linked, and ran successfully |
| Sanitized installation and independent Clang consumer | Built, linked, and ran successfully |
| Individual public headers with `-Wall -Wextra -Wpedantic -Werror` | All compiled successfully |
| Docker image build | Passed |
| Container protocol, UID, persistent volume, restart, and SIGTERM test | Passed |
| `git diff --check` and relative Markdown links | Passed |
| Standalone generic RPC examples and SIGTERM | Passed |
| Network/persistence A/B benchmark | 54 trials, zero request errors; raw results retained |

The four CTest configurations run the same ten test groups. Both persistence-mode integration tests run the read-through profile application. The generic RPC test exercises typed dispatch and remote errors over actual sockets. Batch storage tests verify one fsync for multiple mutations, serial mixed-operation results, invalid-peer isolation, capacity rejection before append, and the documented LRU replay difference.

The integration test starts the actual server executable, exercises the compiled C++ client example, sends maximum-size values and pipelined frames, runs concurrent clients, rejects malformed headers, kills the process after acknowledged writes, verifies all 64 acknowledged concurrent writes after replay, and tests clean shutdown and corrupt-log rejection.

The container test creates its own container and anonymous volume, checks UID `10001`, performs real protocol operations, verifies persisted data after restart, and removes its temporary resources afterward.

## Runtime compatibility finding

GCC ThreadSanitizer could compile the suite but failed to initialize reliably on this host with `FATAL: ThreadSanitizer: unexpected memory mapping`. This occurred before application tests executed. The full suite was rebuilt with Clang 22.1.8 ThreadSanitizer and passed without suppressions or host-kernel configuration changes. The CI ThreadSanitizer job uses Clang; its actual hosted-runner result still needs to be observed.

## Unverified scope

- macOS compilation/runtime behavior and the configured remote GitHub Actions jobs were not executed locally.
- Xmake was unavailable on this host; its updated configurations were not executed.
- The optional historical GoogleTest/Google Benchmark targets were not run.
- The local 54-trial throughput/tail-latency comparison is recorded in [Performance](PERFORMANCE.md). Long-duration soak, fixed-arrival-rate overload, and capacity limits remain unmeasured.
- Process-crash recovery was tested; physical power failure, filesystem corruption, and storage-controller behavior were not simulated.
- Authentication boundaries, backup infrastructure, and deployment-specific resource policies require verification in the target environment.

Re-run the commands in [Contributing](../CONTRIBUTING.md) after changing code, compiler, platform, or deployment configuration. Use [Operations](OPERATIONS.md) to plan workload and recovery validation.
