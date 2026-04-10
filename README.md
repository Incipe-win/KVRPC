# KVRPC

**KVRPC** 是一个基于 C++11/C++17 编写的轻量级、高性能异步 RPC 网络通信框架。它是作为 [KVCache](../KVCache) (基于内存的分布式键值存储数据库) 的网络通信引擎而设计的。

本项目去除了对 gRPC、Protobuf 等重量级第三方库的依赖，完全从零手写序列化与网络传输层，非常适合学习和展示 C++ 现代语法以及网络通信底层的原理，是一个非常具有“内涵”的 C++ 基础架构组件。

## 核心特性（简历亮点）

* **基于编译期多态的序列化引擎**：完全基于 C++11 的可变参数模板 (Variadic Templates) 与 SFINAE 特性，实现基础类型、字符串类型的零依赖二进制封包与解包。
* **线程安全的 TCP 连接池**：利用 `std::mutex` 与 `std::condition_variable` 实现了通信连接的高效复用。结合 `std::shared_ptr` 的自定义删除器 (Custom Deleter)，实现了连接被客户端用完后的自动回收（Auto-Release）。
* **异步非阻塞的 API 抽象模型**：客户端接口底层通过 `std::future` 和 `std::async` 实现网络 I/O 等待与主线程业务逻辑的解耦，使调用网络 RPC 接口像本地异步接口一样自然。
* **完美的 KVCache 业务落地**：通过内部代理层 (Stub)，将面向用户的 `Get/Set` 调用自动组装为包含 `<magic, command, key_len, value_len>` `Header` 和对应 `Payload` 的二进制网络协议报文，直连后端的 KVCache 存储节点。

## 架构设计流

1. **Client / Stub**：向业务开发者提供透明的、面向对象的 `client.Set("key", "value")` 异步接口。
2. **Future / Async 层**：异步发起请求并将网络 I/O 挂起在后台线程，立刻返回 `std::future<T>` 句柄给主线程。
3. **Connection Pool**：从预先分配好的 Socket 队列中，安全地取出一个空闲的 TCP 长连接。
4. **Serializer**：将请求上下文、命令类型直接打包序列化为紧凑的内存字节序列 (`std::vector<uint8_t>`)。
5. **Transport Layer**：通过 TCP Socket 投递字节流至远端 KVCache 服务器。

## 快速编译与运行测试

### 依赖环境
* Linux / macOS
* 支持 C++17 的编译器 (GCC 7+ / Clang 5+)
* [Xmake](https://xmake.io/) 构建管理工具

### 执行编译与测试

```bash
# 配置为 Release 模式并编译整个项目及所有的测试桩
xmake f -m release
xmake 

# 1. 运行泛型序列化引擎单元测试
xmake run test_serializer

# 2. 运行连接池并发获取释放测试
xmake run test_connection_pool

# 3. 运行 RPC Future 异步回调机制全链路测试
xmake run test_rpc_client

# 4. 运行基于协议 Header (protocol.h) 结合 KVCache 的兼容性代理测试
xmake run test_kvcache_client
```
