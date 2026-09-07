# KVCache

KVRPC 2 随附的 Linux 缓存服务，使用 epoll ET 主从 Reactor、异步客户端和专用持久化线程；支持分片 LRU、缓存字节预算、TTL、group commit、CRC 日志校验及原子 AOF 重写。

从仓库根目录使用 CMake 构建，依赖 Protobuf。使用方法、参数和验证说明见 [主 README](../README.md)、[运行说明](../docs/OPERATIONS.md) 和 [架构](../docs/ARCHITECTURE.md)。

原 SET/GET/DEL/STATS 协议保持兼容，新增 LOOKUP 区分未命中与空值，SET_TTL 支持过期时间。服务仍是单节点缓存；读取导致的 LRU 顺序变化不会逐次写入日志，恢复后驻留键集合可能变化。
