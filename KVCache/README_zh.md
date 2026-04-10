# 高性能 C++ 键值缓存系统

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Language](https://img.shields.io/badge/language-C%2B%2B20-blue.svg)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)

[English](README.md) | [中文](README_zh.md)

一个使用现代 C++ 实现的高性能、线程安全的键值缓存系统。
专为 C++ 后端开发面试设计，展示并发、网络编程和系统设计能力。

## 功能特性
- **LRU 淘汰策略**: 高效的 O(1) 淘汰策略。
- **线程安全**: 支持细粒度锁的高并发访问。
- **高性能**: 使用 `epoll` 和 Reactor 模式的非阻塞 I/O。
- **TTL 支持**: 键过期支持。
- **网络**: 自定义 TCP 协议 / HTTP。

## 性能基准测试
`LRUCache` (单互斥锁) 与 `ShardedCache` (16 分片) 在高并发 (随机键) 下的对比。

| 线程数 | LRUCache (ns) | ShardedCache (ns) | 加速比 |
|--------|---------------|-------------------|--------|
| 1      | 34.5          | 45.1              | 0.76x  |
| 4      | 740           | 191               | **3.8x**|
| 8      | 2344          | 313               | **7.5x**|
| 16     | 5369          | 635               | **8.4x**|

*环境: 12 核, Linux x86_64*

## 构建
```bash
xmake
```

## 运行测试
```bash
xmake run test_lru_cache
xmake run test_sharded_cache
```

## 运行基准测试
```bash
xmake run benchmark_cache
```

## 运行服务器
```bash
xmake run kv_server 8080
```

## 可观测性
服务器支持 `STATS` 命令以获取缓存性能指标 (命中/未命中)。
这允许实时监控缓存效率。

## Docker 支持

构建 Docker 镜像:
```bash
docker build -t kvcache .
```

运行容器:
```bash
docker run -p 8080:8080 kvcache
```

## CI/CD

本项目使用 GitHub Actions 进行持续集成。
- **构建与测试**: 每次推送和 PR 时自动构建项目并运行单元测试。
- **基准测试**: 运行性能基准测试以确保无性能回退。
- **Docker**: 验证 Docker 镜像构建。
