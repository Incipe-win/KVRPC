#!/usr/bin/env python3
"""Fixed-arrival-rate RPC probe. Latency starts at scheduled arrival, including load-generator delay."""
import argparse
import asyncio
import hashlib
import json
import os
from pathlib import Path
import platform
import socket
import struct
import subprocess
import time

HEADER = struct.Struct('!4sIQ')
REQUEST = b'\x12\x04ping'  # Envelope.method = "ping"; REQUEST is the default enum.

async def trial(binary, io_threads, idle_count, args):
    with socket.socket() as reserve:
        reserve.bind(('127.0.0.1', 0))
        port = reserve.getsockname()[1]
    process = subprocess.Popen([str(binary), str(port), str(io_threads), str(args.workers)], stdout=subprocess.DEVNULL)
    connections = []
    readers = []
    pending = {}
    latency = []
    failed = 0
    dropped = 0
    deadline = time.monotonic() + 5
    async def receive(reader):
        nonlocal failed
        try:
            while True:
                magic, size, identifier = HEADER.unpack(await reader.readexactly(16))
                if magic != b'KVR2' or size > 1024:
                    raise RuntimeError('Invalid response header')
                payload = await reader.readexactly(size)
                planned = pending.pop(identifier)
                if payload != b'\x08\x01':
                    failed += 1
                else:
                    latency.append((time.monotonic() - planned) * 1e6)
        except asyncio.IncompleteReadError:
            return
    def proc_stats():
        stat = Path(f'/proc/{process.pid}/stat').read_text().split()
        status = Path(f'/proc/{process.pid}/status').read_text().splitlines()
        return (int(stat[13]) + int(stat[14]),
                int(next(line for line in status if line.startswith('VmRSS:')).split()[1]))
    try:
        while True:
            try:
                reader, writer = await asyncio.open_connection('127.0.0.1', port)
                connections.append(writer)
                break
            except OSError:
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('Server did not start')
                await asyncio.sleep(.01)
        writers = []
        for _ in range(args.connections):
            reader, writer = await asyncio.open_connection('127.0.0.1', port)
            connections.append(writer)
            writers.append(writer)
            readers.append(asyncio.create_task(receive(reader)))
        for _ in range(idle_count):
            _, writer = await asyncio.open_connection('127.0.0.1', port)
            connections.append(writer)
        cpu_before, _ = proc_stats()
        start = time.monotonic()
        count = int(args.seconds * args.rate)
        for index in range(count):
            planned = start + index / args.rate
            await asyncio.sleep(max(0, planned - time.monotonic()))
            if len(pending) >= args.max_pending:
                dropped += 1
                continue
            identifier = index + 1
            pending[identifier] = planned
            writer = writers[index % len(writers)]
            writer.write(HEADER.pack(b'KVR2', len(REQUEST), identifier) + REQUEST)
            await writer.drain()
        until = time.monotonic() + 5
        while pending and time.monotonic() < until:
            await asyncio.sleep(.001)
        elapsed = time.monotonic() - start
        cpu_after, rss = proc_stats()
        latency.sort()
        def percentile(q):
            return latency[int((len(latency)-1)*q)] if latency else None
        return dict(io_threads=io_threads, idle=idle_count, offered=count, completed=len(latency),
                    errors=failed, timeouts=len(pending), dropped=dropped, elapsed_seconds=elapsed,
                    ops_per_second=len(latency)/elapsed, p50_us=percentile(.5), p99_us=percentile(.99),
                    server_cpu_seconds=(cpu_after-cpu_before)/os.sysconf('SC_CLK_TCK'), server_rss_kib=rss)
    finally:
        for writer in connections:
            writer.close()
        for task in readers:
            task.cancel()
        await asyncio.gather(*readers, return_exceptions=True)
        process.terminate()
        try:
            await asyncio.to_thread(process.wait, timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()

async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', default='build/review')
    parser.add_argument('--seconds', type=float, default=2)
    parser.add_argument('--rate', type=int, default=5000)
    parser.add_argument('--connections', type=int, default=16)
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('--max-pending', type=int, default=256)
    parser.add_argument('--io', type=int, nargs='+', default=[1, 2, 4])
    parser.add_argument('--idle', type=int, nargs='+', default=[0, 1000])
    parser.add_argument('--output', default='benchmarks/network-results.json')
    args = parser.parse_args()
    binary = Path(args.build).resolve() / 'kvrpc_rpc_server'
    result = dict(platform=platform.platform(), binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                  command=vars(args), trials=[])
    for io in args.io:
        for idle in args.idle:
            item = await trial(binary, io, idle, args)
            result['trials'].append(item)
            print(json.dumps(item), flush=True)
    Path(args.output).write_text(json.dumps(result, indent=2) + '\n')
    if any(t['errors'] or t['timeouts'] for t in result['trials']):
        raise SystemExit(1)

if __name__ == '__main__':
    asyncio.run(main())
