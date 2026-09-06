#include <kvrpc/kvcache_client.h>
#include <kvrpc/rpc_client.h>
int main() {
    kvrpc::Serializer serializer;
    serializer.Serialize(uint32_t(42));
    uint32_t value = 0;
    serializer.Deserialize(value);
    kvrpc::TcpConnection connection;
    return value == 42 && !connection.IsConnected() ? 0 : 1;
}
