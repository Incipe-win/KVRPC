# High-Performance C++ Key-Value Cache

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Language](https://img.shields.io/badge/language-C%2B%2B20-blue.svg)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)

[English](README.md) | [中文](README_zh.md)

A high-performance, thread-safe Key-Value cache system implemented in modern C++.
Designed for C++ Backend Developer interviews, demonstrating Concurrency, Networking, and System Design.

## Features
- **LRU Eviction**: Efficient O(1) eviction policy.
- **Thread Safe**: Concurrent access support with fine-grained locking.
- **High Performance**: Non-blocking I/O using `epoll` and Reactor pattern.
- **TTL Support**: Key expiration.
- **Network**: Custom TCP Protocol / HTTP.

## Performance Benchmark
Comparison between `LRUCache` (Single Mutex) and `ShardedCache` (16 Shards) under high concurrency (Random Keys).

| Threads | LRUCache (ns) | ShardedCache (ns) | Speedup |
|---------|---------------|-------------------|---------|
| 1       | 34.5          | 45.1              | 0.76x   |
| 4       | 740           | 191               | **3.8x**|
| 8       | 2344          | 313               | **7.5x**|
| 16      | 5369          | 635               | **8.4x**|

*Environment: 12 Cores, Linux x86_64*

## Build
```bash
xmake
```

## Run Tests
```bash
xmake run test_lru_cache
xmake run test_sharded_cache
```

## Run Benchmark
```bash
xmake run benchmark_cache
```

## Run Server
```bash
xmake run kv_server 8080
```

## Observability
The server supports a `STATS` command to retrieve cache performance metrics (Hits/Misses).
This allows for monitoring the cache efficiency in real-time.

## Docker Support

Build the Docker image:
```bash
docker build -t kvcache .
```

Run the container:
```bash
docker run -p 8080:8080 kvcache
```

## CI/CD

The project uses GitHub Actions for Continuous Integration.
- **Build & Test**: Automatically builds the project and runs unit tests on every push and PR.
- **Benchmark**: Runs performance benchmarks to ensure no regressions.
- **Docker**: Verifies the Docker image build.



