# Version 2 persistence measurement

CPU: AMD Ryzen 9 7945HX with Radeon Graphics. Platform: `Linux-6.18.33.2-microsoft-standard-WSL2-x86_64-with-glibc2.39`.
Compiler: c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0. Filesystem: ext2/ext3; AOF parent: /tmp.

18 closed-loop trials; 19,367 completed requests; 0 errors. Raw invocation, binary hashes, and revision are in [results-v2.json](../benchmarks/results-v2.json).

| Concurrency | Writes | Always ops/s | Group ops/s | Ratio | Always P99 ms | Group P99 ms | Records/sync |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 50% | 391 | 390 | 1.00x | 5.05 | 5.66 | 1.00 |
| 8 | 50% | 473 | 1,014 | 2.14x | 29.46 | 12.05 | 2.22 |
| 32 | 50% | 480 | 3,619 | 7.54x | 89.05 | 11.85 | 8.15 |

Each load worker uses one persistent connection and one outstanding request. Values are checked and writes await acknowledgement. The modes use the same ET server and one storage execution thread; group combines queued work behind one fsync. Percentiles are medians of per-trial percentiles, not percentiles of merged samples.

These short WSL measurements are not saturation, long-duration, or power-loss tests. The machine was not isolated from other development activity. Storage latency strongly affects the ratio; the result is not an epoll-versus-poll comparison.
