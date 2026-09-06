# Local persistence performance

Measured September 6, 2026. These are local closed-loop TCP measurements, not a production capacity claim.

## Reproduction

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel 4
python3 benchmarks/run.py --seconds 2 --repeats 3 --output benchmarks/results.json
python3 benchmarks/report.py
```

## Environment and method

- CPU: AMD Ryzen 9 7945HX with Radeon Graphics; 16 logical CPUs exposed.
- Platform: `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39`.
- Compiler: c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0; CMake Release build.
- AOF directory: `/tmp`; filesystem reported by `stat -f`: `ext2/ext3`. This is WSL virtualized storage.
- Both modes use the same server binary and event loop. Only per-mutation versus per-ready-batch fsync changes.
- 256-byte values; one persistent connection and one outstanding call per load worker. Client future/executor overhead is included.
- Keys are partitioned across workers, four keys per worker, and preloaded before measurement. Every GET validates the full value; every SET waits for acknowledgement.
- Connection warmup and preload are excluded. Each trial runs for two seconds plus completion of in-flight requests; total completion time is the throughput denominator.
- Three trials per scenario, alternating A/B order by repeat; fresh server and AOF for each trial. No artificial disk delay or relaxed durability.
- Throughput and percentiles below are medians of the three individual trial statistics. Percentiles describe successful request latency; errors are separately counted.
- 54 trials, 154,503 completed requests, 0 request errors. Recorded AOF mutation counts match acknowledged writes in every trial.
- Raw results include the source base revision, dirty-tree flag, benchmarked binary SHA-256 hashes, and invocation. The measured revision is the working-tree implementation, not the unmodified base commit.

## Results

| Concurrency | Writes | Always ops/s | Group ops/s | Ratio | Always P99 ms | Group P99 ms | Group records/fsync |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 10% | 1,375 | 1,367 | 0.99x | 4.64 | 4.62 | 1.00 |
| 1 | 50% | 407 | 408 | 1.00x | 4.90 | 4.91 | 1.00 |
| 1 | 100% | 226 | 227 | 1.01x | 5.26 | 5.45 | 1.00 |
| 8 | 10% | 2,029 | 2,295 | 1.13x | 16.15 | 9.07 | 1.17 |
| 8 | 50% | 483 | 1,021 | 2.11x | 31.37 | 9.94 | 2.16 |
| 8 | 100% | 241 | 950 | 3.94x | 36.97 | 10.60 | 4.00 |
| 32 | 10% | 2,386 | 4,345 | 1.82x | 33.22 | 10.21 | 2.01 |
| 32 | 50% | 474 | 3,591 | 7.58x | 92.63 | 12.32 | 8.19 |
| 32 | 100% | 246 | 3,600 | 14.66x | 133.26 | 10.61 | 16.00 |

## Interpretation

At 32 concurrent clients and 50% writes, grouping reduces the number of storage barriers per mutation, raising median throughput from approximately 474 to 3,591 ops/s. Median trial P99 drops from 92.63 to 12.32 ms. The 32-client write-only scenario reaches approximately 3,600 ops/s, versus 246 with per-mutation fsync.

Single-client results remain essentially unchanged because there are no simultaneous mutations to combine. This is an important control: the gain comes from amortizing synchronization, not acknowledging before fsync. Read-heavy traffic gains less, while individual reads in a mixed batch still wait behind its durability barrier.

The baseline already uses the updated transport. These numbers do not compare against Redis, an older repository commit, an in-memory cache microbenchmark, or an asynchronous storage engine. The relatively expensive fsync on this host makes batching particularly effective; faster storage can produce a different ratio.

The harness is closed-loop: clients slow down with the server. It does not establish latency at a fixed external arrival rate, sustained overload behavior, byte-capacity limits, or long-duration stability. The three short repetitions describe this experiment, not a confidence interval. Replay tests cover process death; storage power loss is outside this measurement.

[Raw trials](../benchmarks/results.json) · [Benchmark source](../benchmarks/kv_bench.cpp) · [Architecture and durability contract](ARCHITECTURE.md)
