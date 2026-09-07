# Version 2 performance evidence

## Fixed-arrival-rate network probe

Run: `python3 benchmarks/network.py --build build/review --output benchmarks/network-results.json`.

16 active connections, 5,000 scheduled requests/second, two seconds per scenario, four business workers. Each request is a Protobuf ping over persistent TCP. Latency starts at scheduled arrival and includes load-generator scheduling delay. All six scenarios completed 10,000 requests each with zero errors, timeouts, or dropped admissions.

| I/O threads | Idle connections | Completed ops/s | P99 ms | Server CPU seconds | Server RSS KiB |
| --- | --- | --- | --- | --- | --- |
| 1 | 0 | 4997 | 1.026 | 0.67 | 6160 |
| 1 | 1000 | 4996 | 1.033 | 0.66 | 7196 |
| 2 | 0 | 4996 | 0.876 | 0.85 | 6172 |
| 2 | 1000 | 4996 | 1.020 | 0.88 | 7220 |
| 4 | 0 | 4997 | 0.843 | 0.88 | 6236 |
| 4 | 1000 | 4996 | 1.007 | 0.92 | 7344 |

This measures behavior at an offered rate of 5,000/s, not maximum throughput. More I/O threads did not increase throughput under this capped load. RSS excludes kernel socket memory. One short trial per scenario does not establish scaling confidence or long-duration stability.

[Raw network results](../benchmarks/network-results.json) include the server binary hash and platform. Use `--rate`, `--seconds`, `--io`, `--idle`, and `--max-pending` to explore load; admission drops remain visible.

## Persistence

[Version-2 A/B report](PERSISTENCE_V2.md) and [18 raw trials](../benchmarks/results-v2.json) compare always/group fsync. These isolate the persistence policy within the new architecture; they do not isolate the benefit of epoll ET.

## Historical evidence

[Version-1 report](PERFORMANCE_V1.md) and [original raw trials](../benchmarks/results.json) are retained for provenance. Do not mix their numbers with the current implementation.
