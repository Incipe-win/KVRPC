#include <kvrpc/rpc_client.h>
#include <iostream>
int main(int argc, char** argv) {
    try {
        auto pool = std::make_shared<kvrpc::ConnectionPool>("127.0.0.1", argc > 1 ? std::stoi(argv[1]) : 8081, 4);
        kvrpc::RpcClient client(pool);
        std::cout << "add(20, 22) = " << client.Call<int64_t>("add", int32_t(20), int32_t(22)).get() << '\n';
        std::cout << client.Call<std::string>("echo", "RPC round trip").get() << '\n';
        client.Call<void>("ping").get();
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
