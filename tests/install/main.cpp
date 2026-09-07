#include <kvrpc/kvcache_client.h>
#include <kvrpc/rpc_client.h>
#include <kvrpc/rpc_server.h>
#include <kvrpc/serializer.h>
int main() {
    kvrpc::RpcServer server(0);
    server.Register<int32_t, int32_t>("echo", [](int32_t value) { return value; });
    kvrpc::Serializer serializer;
    serializer.Serialize(uint32_t(42));
    uint32_t value = 0;
    serializer.Deserialize(value);
    kvrpc::TcpConnection connection;
    return value == 42 && !connection.IsConnected() ? 0 : 1;
}
