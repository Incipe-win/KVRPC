#include "kvrpc/rpc_server.h"
#include "kvrpc/rpc_client.h"
#include "test_support.h"

struct Running {
    kvrpc::RpcServer server{0};
    std::future<void> task;
    ~Running() { server.Stop(); if (task.valid()) task.wait(); }
    void Start() {
        task = std::async(std::launch::async, [&] { server.Start(); });
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!server.Port()) {
            if (task.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready) task.get();
            CHECK(std::chrono::steady_clock::now() < deadline);
        }
    }
};
int main() {
    Running running;
    running.server.Register<int64_t, int32_t, int32_t>("add", [](int32_t a, int32_t b) { return int64_t(a) + b; });
    running.server.Register<std::string, std::string>("echo", [](const std::string& s) { return s; });
    running.server.Register<void>("ping", [] {});
    running.server.Register<void>("fail", [] { throw std::runtime_error("private internal detail"); });
    Throws([&] { running.server.Register<void>("ping", [] {}); });
    running.Start();
    auto pool = std::make_shared<kvrpc::ConnectionPool>("127.0.0.1", running.server.Port(), 8);
    kvrpc::RpcClient client(pool);
    CHECK(client.Call<int64_t>("add", int32_t(20), int32_t(22)).get() == 42);
    std::string binary("a\0b", 3);
    CHECK(client.Call<std::string>("echo", binary).get() == binary);
    client.Call<void>("ping").get();
    for (const auto& test : std::vector<std::pair<std::string, uint32_t>>{{"missing", 1}, {"add", 2}, {"fail", 3}}) {
        bool caught = false;
        try { client.Call<void>(test.first).get(); }
        catch (const kvrpc::RemoteError& error) {
            CHECK(error.status() == test.second);
            CHECK(std::string(error.what()).find("private") == std::string::npos);
            caught = true;
        }
        CHECK(caught);
        client.Call<void>("ping").get();
    }
    ErrorIs(kvrpc::ErrorCode::remote, [&] { client.Call<int64_t>("add", int32_t(1), int32_t(2), int32_t(3)).get(); });
    std::vector<std::future<int64_t>> futures;
    for (int32_t i = 0; i < 32; ++i) futures.push_back(client.Call<int64_t>("add", i, int32_t(100)));
    for (int i = 0; i < 32; ++i) CHECK(futures[i].get() == i + 100);
    // Real server handles fragmented headers and pipelined frames on one connection.
    auto connection = pool->Acquire();
    kvrpc::Serializer body; body.Serialize(std::string("ping"));
    kvrpc::Serializer prefix; prefix.Serialize(static_cast<uint32_t>(body.GetBuffer().size()));
    for (char byte : prefix.GetBuffer()) CHECK(connection->SendAll(&byte, 1));
    CHECK(connection->SendAll(body.GetBuffer().data(), body.GetBuffer().size()));
    CHECK(connection->SendAll(prefix.GetBuffer().data(), 4));
    CHECK(connection->SendAll(body.GetBuffer().data(), body.GetBuffer().size()));
    char replies[8]; CHECK(connection->RecvAll(replies, 8));
    for (char byte : replies) CHECK(byte == 0);
    const char oversized[] = {char(255), char(255), char(255), char(127)};
    CHECK(connection->SendAll(oversized, 4));
    CHECK(!connection->RecvAll(replies, 1));
    running.server.Stop(); running.task.get();
}
