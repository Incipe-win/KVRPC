#pragma once

#include "kvrpc/connection_pool.h"
#include "kvrpc/serializer.h"
#include <future>
#include <memory>
#include <stdexcept>
#include <string>

namespace kvrpc {

class RpcClient {
private:
  std::shared_ptr<ConnectionPool> pool_;

public:
  RpcClient(std::shared_ptr<ConnectionPool> pool) : pool_(std::move(pool)) {}

  template <typename RetType, typename... Args>
  std::future<RetType> Call(const std::string &rpc_name, Args... args) {
    // 使用 std::async 开启一个后台异步任务执行网络 I/O，不阻塞主线程
    return std::async(std::launch::async,
                      [this, rpc_name, args...]() -> RetType {
                        // 1. 序列化请求数据
                        Serializer sz;
                        sz.Serialize(rpc_name, args...);

                        const auto &payload = sz.GetBuffer();
                        uint32_t len = payload.size();

                        // 2. 从连接池获取 TCP 连接
                        auto conn = pool_->Acquire();
                        if (!conn->IsConnected()) {
                          throw std::runtime_error(
                              "RPC Call Failed: Cannot connect to server");
                        }

                        // 3. 发送长度头部 + payload 载荷
                        if (!conn->SendAll(reinterpret_cast<const char *>(&len),
                                           sizeof(len))) {
                          throw std::runtime_error(
                              "RPC Call Failed: Network send error (length)");
                        }
                        if (!conn->SendAll(payload.data(), payload.size())) {
                          throw std::runtime_error(
                              "RPC Call Failed: Network send error (payload)");
                        }

                        // 4. 接收响应的长度头部
                        uint32_t resp_len = 0;
                        if (!conn->RecvAll(reinterpret_cast<char *>(&resp_len),
                                           sizeof(resp_len))) {
                          throw std::runtime_error(
                              "RPC Call Failed: Network recv error (length)");
                        }

                        // 5. 接收响应 payload
                        std::vector<char> resp_buf(resp_len);
                        if (!conn->RecvAll(resp_buf.data(), resp_len)) {
                          throw std::runtime_error(
                              "RPC Call Failed: Network recv error (payload)");
                        }

                        // 6. 反序列化成指定返回类型 RetType
                        Serializer r_sz(resp_buf);
                        RetType ret;
                        r_sz.Deserialize(ret);

                        return ret; // 自动装填到 std::future 结果中
                      });
  }
};

} // namespace kvrpc
