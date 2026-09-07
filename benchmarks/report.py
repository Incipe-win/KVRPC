#!/usr/bin/env python3
"""Render measured per-trial medians without hard-coded throughput claims."""
import argparse
import json
from pathlib import Path
import statistics
parser = argparse.ArgumentParser()
parser.add_argument('--input', default='benchmarks/results-v2.json')
parser.add_argument('--output', default='docs/PERSISTENCE_V2.md')
args = parser.parse_args()
data = json.loads(Path(args.input).read_text())
trials = data['trials']
env = data['environment']
lines = ['# Version 2 persistence measurement', '', f'CPU: {env["cpu_model"]}. Platform: `{env["platform"]}`.',
         f'Compiler: {env["compiler"]}. Filesystem: {env["filesystem"]}; AOF parent: {env["aof_parent"]}.', '',
         f'{len(trials)} closed-loop trials; {sum(t["completed"] for t in trials):,} completed requests; '
         f'{sum(t["errors"] for t in trials)} errors. Raw invocation, binary hashes, and revision are in [{Path(args.input).name}](../{args.input}).', '',
         '| Concurrency | Writes | Always ops/s | Group ops/s | Ratio | Always P99 ms | Group P99 ms | Records/sync |',
         '| --- | --- | --- | --- | --- | --- | --- | --- |']
for concurrency, writes in sorted({(t['concurrency'], t['write_percent']) for t in trials}):
    groups = {mode: [t for t in trials if t['concurrency']==concurrency and t['write_percent']==writes and t['mode']==mode] for mode in ['always','group']}
    def med(mode, field): return statistics.median(t[field] for t in groups[mode])
    a, g = med('always','ops_per_second'), med('group','ops_per_second')
    batches=[t['aof_records']/t['aof_syncs'] for t in groups['group'] if t['aof_syncs']]
    batch=statistics.median(batches) if batches else 0
    lines.append(f'| {concurrency} | {writes}% | {a:,.0f} | {g:,.0f} | {g/a:.2f}x | {med("always","p99_us")/1000:.2f} | {med("group","p99_us")/1000:.2f} | {batch:.2f} |')
lines += ['', 'Each load worker uses one persistent connection and one outstanding request. Values are checked and writes await acknowledgement. '
          'The modes use the same ET server and one storage execution thread; group combines queued work behind one fsync. '
          'Percentiles are medians of per-trial percentiles, not percentiles of merged samples.', '',
          'These short WSL measurements are not saturation, long-duration, or power-loss tests. The machine was not isolated from other development activity. '
          'Storage latency strongly affects the ratio; the result is not an epoll-versus-poll comparison.', '']
Path(args.output).write_text('\n'.join(lines))
