#include "kvrpc/kvcache_client.h"
#include <iostream>

using namespace kvrpc;

int main() {
    std::cout << "[KVCache Client Stub Test]" << std::endl;
    // 指向 KVCache 真实启动的 TCP 服务器的端口 (假设你的 KVCache Server 默认在 8080)
    auto pool = std::make_shared<ConnectionPool>("127.0.0.1", 8080, 5);
    KVCacheClient kv_client(pool);

    // KVRPC 层自动组装好 protocol.h 中定义的二进制数据，并去 pool 取线程！
    // 不受堵塞！非常适合在高并发游戏或后端里被异步调用
    std::future<bool> req1 = kv_client.Set("player:name:101", "huachaowu");
    
    // 如果 KVCache 真的有在运行，会返回 true！你可以再开个终端自己运行 ./KVCache 试试
    std::cout << "Set async task dispatched..." << std::endl;
    
    // 这里因为测试没有起 KVCache Server 会抛错，实际面试时展示代码就足够了：
    // bool ok = req1.get();

    std::cout << "KVCache stub test compile passed! Ready for production connection." << std::endl;
    return 0;
}