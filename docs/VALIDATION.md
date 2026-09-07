# Version 2 validation

Local Linux x86_64 / WSL2 verification on September 6, 2026. GCC 13.3.0 Release, Clang 22.1.8 sanitizer builds, Protobuf 3.21.12. This records local execution, not a remote CI result.

| Check | Result |
| --- | --- |
| Release build and CTest | 12/12 passed |
| Clang ASan + UBSan | 12/12 passed |
| Clang TSan | Initial full runs passed; repeated client-error tests expose the standard-library exception lifetime report described below. Not a clean TSan certification |
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

On this host the online symbolizer can also hang while reporting it. `symbolize=0` obtains the raw report without suppressing race checking; addresses were resolved locally with addr2line/GDB. No TSan suppressions or ignored tests were added. A clean repeated TSan run requires a compatible instrumented C++ runtime; it is still an outstanding environment verification, not a claim that every possible project race is excluded.

## Limits

Xmake and hosted CI were not executed. Version 2 is Linux-only. Short benchmarks do not establish sustained overload capacity or long-duration stability. Actual power loss and storage-controller behavior were not simulated. The container contains the KV application; generic RPC consumers are verified through CMake and native tests.
