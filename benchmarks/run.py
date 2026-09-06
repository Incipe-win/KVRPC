#!/usr/bin/env python3
"""Matched closed-loop A/B runs; each trial starts a fresh real server and AOF."""
import argparse
import json
import hashlib
import os
from pathlib import Path
import platform
import socket
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('--build', default='build/release')
parser.add_argument('--seconds', type=float, default=2)
parser.add_argument('--repeats', type=int, default=3)
parser.add_argument('--concurrency', type=int, nargs='+', default=[1, 8, 32])
parser.add_argument('--writes', type=int, nargs='+', default=[10, 50, 100])
parser.add_argument('--value-bytes', type=int, default=256)
parser.add_argument('--output', default='benchmarks/results.json')
parser.add_argument('--data-dir', default=None, help='Parent for temporary AOF files; controls storage filesystem')
args = parser.parse_args()
build = Path(args.build).resolve()
result = {'environment': {'platform': platform.platform(), 'cpu_count': os.cpu_count(),
          'compiler': subprocess.check_output(['c++', '--version'], text=True).splitlines()[0],
          'build_type': 'Release (see build cache)',
          'binary_sha256': {name: hashlib.sha256((build / name).read_bytes()).hexdigest() for name in ['kv_server', 'kv_bench']},
          'cpu_model': next((line.split(':', 1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines()
                             if line.startswith('model name')), 'unknown'),
          'source_revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
          'working_tree_dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], text=True)),
          'timestamp_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
          'command': vars(args)}, 'trials': []}
output = Path(args.output)
output.parent.mkdir(parents=True, exist_ok=True)
for repeat in range(args.repeats):
    for concurrency in args.concurrency:
        for writes in args.writes:
            for mode in (['always', 'group'] if repeat % 2 == 0 else ['group', 'always']):
                with tempfile.TemporaryDirectory(prefix='kvrpc-bench-', dir=args.data_dir) as directory:
                    result['environment']['aof_parent'] = str(Path(directory).parent)
                    result['environment']['filesystem'] = subprocess.check_output(
                        ['stat', '-f', '-c', '%T', directory], text=True).strip()
                    with socket.socket() as reserve:
                        reserve.bind(('127.0.0.1', 0))
                        port = reserve.getsockname()[1]
                    with open(Path(directory) / 'server.log', 'w+') as log:
                        server = subprocess.Popen([str(build / 'kv_server'), str(port), str(Path(directory) / 'data.aof'),
                                                   '127.0.0.1', mode], stdout=log, stderr=log)
                        try:
                            deadline = time.monotonic() + 5
                            while True:
                                if server.poll() is not None or time.monotonic() > deadline:
                                    raise RuntimeError('Server failed to start')
                                try:
                                    with socket.create_connection(('127.0.0.1', port), timeout=.2):
                                        break
                                except OSError:
                                    time.sleep(.01)
                            trial = json.loads(subprocess.check_output([str(build / 'kv_bench'), str(port),
                                str(concurrency), str(args.seconds), str(writes), str(args.value_bytes)],
                                text=True, timeout=args.seconds + 30))
                            trial.update(mode=mode, repeat=repeat)
                            result['trials'].append(trial)
                            output.write_text(json.dumps(result, indent=2) + '\n')
                            print(f'{mode:6} c={concurrency:2} w={writes:3}% {trial["ops_per_second"]:9.0f} ops/s '
                                  f'p99={trial["p99_us"]:8.0f}us syncs={trial["aof_syncs"]}', flush=True)
                        finally:
                            if server.poll() is None:
                                server.terminate()
                            try:
                                server.wait(timeout=5)
                            except subprocess.TimeoutExpired:
                                server.kill()
                                server.wait()
print(output)
