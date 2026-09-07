# Resume wording and interview evidence

**KVRPC：基于 Reactor 的 C++ RPC 与持久化缓存服务**

技术栈：C++17、Linux、epoll ET、eventfd、Protobuf、线程池、分片 LRU、AOF、CMake。

- 实现主从 Reactor 网络模型，连接固定归属 I/O 线程，通过 ET 就绪通知、跨线程任务唤醒、定时器和缓冲区游标处理部分读写、超时与背压。
- 实现 Protobuf RPC 协议及事件驱动客户端，利用 request ID 支持同连接多请求在途与乱序响应匹配，并通过有界准入和整体 deadline 控制请求积压。
- 将业务回调和持久化移出网络线程，使用专用存储线程完成 group commit；实现 TTL、缓存字节预算、CRC 日志校验及原子 AOF 重写。
- 建立真实网络、半关闭、大包、慢回调隔离、超时、TTL 恢复和日志损坏测试，并提供固定到达率网络测量与持久化 A/B 工具。

具体性能指标以 [Performance](PERFORMANCE.md) 中对应版本、配置和原始结果为准。旧 poll 版本的提升倍数不能作为当前 Reactor 实现的性能证据。

面试应说明：KV 操作仍由一个存储执行线程排序；网络多 Reactor 不等于缓存分片并行；request ID 不等于幂等去重；TTL 依赖墙上时钟；AOF 重写暂停存储执行但不阻塞网络循环；读驱动的 LRU 顺序未逐次持久化。
