#include "kvrpc/rpc_client.h"
#include "kvrpc/rpc_server.h"
#include "test_support.h"
using namespace std::chrono_literals;
struct Server {
    kvrpc::RpcServer rpc{0};
    std::future<void> thread;
    ~Server() {
        rpc.Stop();
        if (thread.valid()) thread.wait();
    }
    void Start() {
        thread = std::async(std::launch::async, [&] { rpc.Start(); });
        while (!rpc.Port()) {
            if (thread.wait_for(1ms) == std::future_status::ready) thread.get();
        }
    }
};
int main() {
    Server server;
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    server.rpc.Register<int32_t>("slow", [&] {
        entered.set_value();
        gate.wait();
        return 1;
    });
    server.rpc.Register<int32_t, int32_t>("echo", [](int32_t n) { return n; });
    server.rpc.Register<std::string, std::string>("large", [](const std::string& text) { return text; });
    server.Start();
    // One TCP connection: a blocked callback must not block a later response.
    kvrpc::RpcClient client(std::make_shared<kvrpc::ConnectionPool>("127.0.0.1", server.rpc.Port(), 1));
    auto slow = client.Call<int32_t>("slow");
    entered.get_future().wait();
    auto fast = client.Call<int32_t>("echo", int32_t(42));
    bool isolated = fast.wait_for(1s) == std::future_status::ready;
    release.set_value();
    CHECK(isolated && fast.get() == 42 && slow.get() == 1);
    std::string large(1024 * 1024, 'z');
    CHECK(client.Call<std::string>("large", large).get() == large);
    // ET accept rescheduling and a large idle set cannot hide an active connection.
    std::vector<int> idle;
    for (int i = 0; i < 300; ++i) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        CHECK(fd >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(server.rpc.Port());
        CHECK(connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        idle.push_back(fd);
    }
    CHECK(client.Call<int32_t>("echo", int32_t(7)).get() == 7);
    for (int fd : idle) close(fd);
    // A half-closed writer still receives the complete response.
    kvrpc::wire::Envelope request;
    request.set_method("echo");
    request.add_arguments()->set_signed_value(9);
    auto frame = kvrpc::RpcEncode(99, request, 1024);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(server.rpc.Port());
    CHECK(connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    Write(fd, reinterpret_cast<char*>(frame.data()), frame.size());
    shutdown(fd, SHUT_WR);
    frame.resize(kvrpc::RPC_HEADER);
    Read(fd, reinterpret_cast<char*>(frame.data()), frame.size());
    auto size = kvrpc::RpcFrameSize(frame.data(), frame.size());
    frame.resize(size);
    Read(fd, reinterpret_cast<char*>(frame.data() + kvrpc::RPC_HEADER), size - kvrpc::RPC_HEADER);
    CHECK(kvrpc::RpcDecode(frame).result().signed_value() == 9);
    close(fd);
    server.rpc.Stop();
    server.thread.get();
    // Deadline covers queued requests; late multiplexed replies do not corrupt following calls.
    Server timed;
    timed.rpc.Register<int32_t>("delay", [] {
        std::this_thread::sleep_for(160ms);
        return 1;
    });
    timed.rpc.Register<int32_t>("ping", [] { return 2; });
    timed.Start();
    kvrpc::ClientOptions options;
    options.queue_capacity = 1;
    kvrpc::RpcClient bounded(
        std::make_shared<kvrpc::ConnectionPool>("127.0.0.1", timed.rpc.Port(), 1, kvrpc::TransportOptions{100ms, 60ms}),
        options);
    auto pending = bounded.Call<int32_t>("delay");
    ErrorIs(kvrpc::ErrorCode::overloaded, [&] { bounded.Call<int32_t>("ping").get(); });
    ErrorIs(kvrpc::ErrorCode::timeout, [&] { pending.get(); });
    CHECK(bounded.Call<int32_t>("ping").get() == 2);
    std::this_thread::sleep_for(180ms);
    CHECK(bounded.Call<int32_t>("ping").get() == 2);
    kvrpc::ClientOptions serial;
    serial.max_inflight = 1;
    serial.queue_capacity = 2;
    kvrpc::RpcClient queued(kvrpc::ClientEndpoint{"127.0.0.1", timed.rpc.Port(), 1, {100ms, 60ms}}, serial);
    auto first = queued.Call<int32_t>("delay");
    auto second = queued.Call<int32_t>("ping");
    ErrorIs(kvrpc::ErrorCode::timeout, [&] { first.get(); });
    ErrorIs(kvrpc::ErrorCode::timeout, [&] { second.get(); });
}
