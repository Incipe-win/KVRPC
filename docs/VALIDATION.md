# Version 2 validation

Local Linux x86_64 / WSL2 verification on September 6, 2026. GCC 13.3.0 Release, Clang 22.1.8 sanitizer builds, Protobuf 3.21.12. This records local execution, not a remote CI result.

| Check | Result |
| --- | --- |
| Release build and CTest | 12/12 passed |
| Clang ASan + UBSan | 12/12 passed |
| Clang TSan | Fixed on September 7: 14/14 passed, including runtime regression and positive race-detection control; four exception-related tests each passed 20 consecutive runs |
| Independent installed CMake consumer | Build, link, execution passed |
| All public headers independently, warnings as errors | Passed |
| Docker build and process/volume test | Passed: non-root UID, real TCP, persisted data after restart, SIGTERM |
| Network fixed-arrival-rate probe | 60,000 completed requests, zero errors/timeouts/drops, 0/1000 idle connections and 1/2/4 I/O threads |
| Persistence A/B | 18 trials with value/acknowledgement and mutation-count checks; see performance report |

## Regression coverage

The original protocol, serializer, executor, connection ownership, process-death and storage tests remain. New tests cover RPC correlation and same-connection out-of-order responses, slow callback isolation, 1 MiB ET payloads, 300 idle connections/accept continuation, half-close response delivery, bounded admission, request and queue deadlines, late replies, empty-versus-missing lookup, persistent TTL, automatic/explicit AOF rewriting, checksum corruption and cache byte eviction.

## TSan runtime finding

Repeated `kvcache_client` runs report an exception object's last release inside the host's uninstrumented `libstdc++.so.6`, racing with a caught exception read. This remains reproducible independently of KVRPC:

```sh
clang++-22 -std=c++17 -g -O1 -fsanitize=thread -pthread \
  tests/diagnostics/future_exception_tsan.cpp -o /tmp/future_exception_tsan
TSAN_OPTIONS=symbolize=0 /tmp/future_exception_tsan
```

The [minimal program](../tests/diagnostics/future_exception_tsan.cpp) uses only standard promise/future/thread and an integer exception. The [captured report](../tests/diagnostics/future_exception_tsan.log) has the same `libstdc++ exception_ptr::_M_release` free-versus-read pattern. GCC documents limitations when runtime synchronization is outside instrumentation: [maintainer discussion](https://gcc.gnu.org/pipermail/gcc-bugs/2021-December/771131.html).

The CI fix rebuilds the affected GCC 14.2 exception reference-count operations with TSan, using unmodified upstream source and ELF symbol interposition. This exposes their real atomic release/acquire operations; it does not suppress reports. The adapter is enabled only by `KVRPC_TSAN` on Linux x86_64 with libstdc++ 13/14 headers. CI pins Clang 18 and GCC 14; local verification used Clang 22.1.8. See [runtime provenance and scope](../third_party/gcc-14.2.0/README.md).

The full TSan suite now includes `tsan_exception_runtime` (the independent standard-library regression linked against the instrumented runtime) and `tsan_detector` (an intentionally racy process must exit with a TSan diagnostic). All 14 tests passed locally. The runtime regression plus rpc_client, rpc_server, and kvcache_client each passed 20 consecutive runs. Existing Release/ASan coverage remains enabled.

The historical raw log is retained for comparison. `symbolize=0` is used only by the positive control to avoid external symbolizer dependencies while checking its deliberate diagnostic; normal project tests retain regular reporting.

## Limits

Xmake and hosted CI were not executed. Version 2 is Linux-only. Short benchmarks do not establish sustained overload capacity or long-duration stability. Actual power loss and storage-controller behavior were not simulated. The container contains the KV application; generic RPC consumers are verified through CMake and native tests.
