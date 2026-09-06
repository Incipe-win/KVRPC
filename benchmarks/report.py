#!/usr/bin/env python3
"""Render per-scenario medians from raw trials; never merge trial percentiles."""
import json
from pathlib import Path
import statistics

root = Path(__file__).resolve().parents[1]
data = json.loads((root / 'benchmarks/results.json').read_text())
trials = data['trials']
env = data['environment']
lines = ['# Local persistence performance', '',
    'Measured September 6, 2026. These are local closed-loop TCP measurements, not a production capacity claim.', '',
    '## Reproduction', '', '```sh',
    'cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release',
    'cmake --build build/release --parallel 4',
    'python3 benchmarks/run.py --seconds 2 --repeats 3 --output benchmarks/results.json',
    'python3 benchmarks/report.py', '```', '',
    '## Environment and method', '',
    f'- CPU: {env["cpu_model"]}; {env["cpu_count"]} logical CPUs exposed.',
    f'- Platform: `{env["platform"]}`.',
    f'- Compiler: {env["compiler"]}; CMake Release build.',
    f'- AOF directory: `{env["aof_parent"]}`; filesystem reported by `stat -f`: `{env["filesystem"]}`. This is WSL virtualized storage.',
    '- Both modes use the same server binary and event loop. Only per-mutation versus per-ready-batch fsync changes.',
    '- 256-byte values; one persistent connection and one outstanding call per load worker. Client future/executor overhead is included.',
    '- Keys are partitioned across workers, four keys per worker, and preloaded before measurement. Every GET validates the full value; every SET waits for acknowledgement.',
    '- Connection warmup and preload are excluded. Each trial runs for two seconds plus completion of in-flight requests; total completion time is the throughput denominator.',
    '- Three trials per scenario, alternating A/B order by repeat; fresh server and AOF for each trial. No artificial disk delay or relaxed durability.',
    '- Throughput and percentiles below are medians of the three individual trial statistics. Percentiles describe successful request latency; errors are separately counted.',
    f'- {len(trials)} trials, {sum(t["completed"] for t in trials):,} completed requests, {sum(t["errors"] for t in trials)} request errors. Recorded AOF mutation counts match acknowledged writes in every trial.',
    '- Raw results include the source base revision, dirty-tree flag, benchmarked binary SHA-256 hashes, and invocation. The measured revision is the working-tree implementation, not the unmodified base commit.', '',
    '## Results', '',
    '| Concurrency | Writes | Always ops/s | Group ops/s | Ratio | Always P99 ms | Group P99 ms | Group records/fsync |',
    '| --- | --- | --- | --- | --- | --- | --- | --- |']
for concurrency in sorted({t['concurrency'] for t in trials}):
    for writes in sorted({t['write_percent'] for t in trials}):
        groups = {mode: [t for t in trials if t['concurrency'] == concurrency and t['write_percent'] == writes and t['mode'] == mode]
                  for mode in ['always', 'group']}
        def med(mode, field):
            return statistics.median(t[field] for t in groups[mode])
        a, g = med('always', 'ops_per_second'), med('group', 'ops_per_second')
        batch = statistics.median(t['aof_records'] / t['aof_syncs'] for t in groups['group'])
        lines.append(f'| {concurrency} | {writes}% | {a:,.0f} | {g:,.0f} | {g/a:.2f}x | '
                     f'{med("always", "p99_us")/1000:.2f} | {med("group", "p99_us")/1000:.2f} | {batch:.2f} |')
lines += ['', '## Interpretation', '',
    'At 32 concurrent clients and 50% writes, grouping reduces the number of storage barriers per mutation, '
    'raising median throughput from approximately 474 to 3,591 ops/s. Median trial P99 drops from 92.63 to 12.32 ms. '
    'The 32-client write-only scenario reaches approximately 3,600 ops/s, versus 246 with per-mutation fsync.', '',
    'Single-client results remain essentially unchanged because there are no simultaneous mutations to combine. '
    'This is an important control: the gain comes from amortizing synchronization, not acknowledging before fsync. '
    'Read-heavy traffic gains less, while individual reads in a mixed batch still wait behind its durability barrier.', '',
    'The baseline already uses the updated transport. These numbers do not compare against Redis, an older repository commit, '
    'an in-memory cache microbenchmark, or an asynchronous storage engine. The relatively expensive fsync on this host '
    'makes batching particularly effective; faster storage can produce a different ratio.', '',
    'The harness is closed-loop: clients slow down with the server. It does not establish latency at a fixed external '
    'arrival rate, sustained overload behavior, byte-capacity limits, or long-duration stability. The three short repetitions '
    'describe this experiment, not a confidence interval. Replay tests cover process death; storage power loss is outside this measurement.', '',
    '[Raw trials](../benchmarks/results.json) · [Benchmark source](../benchmarks/kv_bench.cpp) · '
    '[Architecture and durability contract](ARCHITECTURE.md)', '']
(root / 'docs/PERFORMANCE.md').write_text('\n'.join(lines))
