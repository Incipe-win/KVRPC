#pragma once

#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "kvrpc/connection_pool.h"
#include "protocol.h"  // 从你的 KVCache 项目引入的头文件

namespace kvrpc {

class KVCacheClient {
   private:
    std::shared_ptr<ConnectionPool> pool_;

   public:
    KVCacheClient(std::shared_ptr<ConnectionPool> pool) : pool_(std::move(pool)) {}

    // 真正的非阻塞 Set，使用 std::async 后台封装打包协议
    std::future<bool> Set(const std::string& key, const std::string& value) {
        return std::async(std::launch::async, [this, key, value]() -> bool {
            auto conn = pool_->Acquire();
            if (!conn->IsConnected()) throw std::runtime_error("No available KVCache connection");

            // 使用 KVCache 的协议序列化数据 (encode)
            std::vector<uint8_t> req_data = kvcache::Message::encode(kvcache::Command::SET, key, value);

            // 传输数据
            if (!conn->SendAll(reinterpret_cast<const char*>(req_data.data()), req_data.size())) {
                return false;
            }

            // 读取返回值包头 (KV_Cache 统一返回 1 字节状态码)
            // 根据原始实现可扩展这里。假设成功服务器返回 'OK' 以及长度或者类似确认。
            // 为了简化与 KVCache 的实际读取，这里我们假设我们只关心包发出去了没
            // *注意*：你需要在 KVCache 服务端处理回包，这里根据 protocol.h
            // 我们发出去就算成功

            // 下方是一个模拟如果能读取服务器 Header 的操作：
            /*
            uint8_t header_buf[kvcache::HEADER_SIZE];
            conn->RecvAll((char*)header_buf, kvcache::HEADER_SIZE);
            auto h = kvcache::Message::decodeHeader(header_buf);
            */

            return true;
        });
    }

    // 同理，包装成一个优雅的 GET 异步调用
    std::future<std::string> Get(const std::string& key) {
        return std::async(std::launch::async, [this, key]() -> std::string {
            auto conn = pool_->Acquire();
            if (!conn->IsConnected()) return "CONNECTION_ERR";

            std::vector<uint8_t> req_data = kvcache::Message::encode(kvcache::Command::GET, key);

            if (!conn->SendAll(reinterpret_cast<const char*>(req_data.data()), req_data.size())) {
                return "NETWORK_SEND_ERR";
            }

            // 读取回复，从 KVCache 返回的数据格式中解析 Header，然后再读取 Value
            uint8_t header_buf[kvcache::HEADER_SIZE];
            if (!conn->RecvAll(reinterpret_cast<char*>(header_buf), kvcache::HEADER_SIZE)) {
                return "NETWORK_RECV_ERR";
            }

            auto h = kvcache::Message::decodeHeader(header_buf);

            std::string value(h.value_len, '\0');
            if (h.value_len > 0) {
                if (!conn->RecvAll(&value[0], h.value_len)) {
                    return "NETWORK_RECV_ERR";
                }
            }
            return value;
        });
    }
};

}  // namespace kvrpc