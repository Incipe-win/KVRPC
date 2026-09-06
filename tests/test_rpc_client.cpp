#include "kvrpc/rpc_client.h"
#include "test_support.h"

std::vector<char> Request(int fd) {
    std::vector<char> header(4); Read(fd, header.data(), 4);
    kvrpc::Serializer s(std::move(header)); uint32_t size; s.Deserialize(size); CHECK(size < 1024);
    std::vector<char> body(size); Read(fd, body.data(), body.size()); return body;
}
void Response(int fd, const std::vector<char>& body) {
    kvrpc::Serializer s; s.Serialize(static_cast<uint32_t>(body.size()));
    Write(fd, s.GetBuffer().data(), 4, true); Write(fd, body.data(), body.size(), true);
}
int main() {
    TestServer server([](int fd) {
        for (int i = 0; i < 3; ++i) {
            kvrpc::Serializer s(Request(fd)); std::string method, text;
            s.Deserialize(method, text); CHECK(method == "echo" && text == "hello");
            kvrpc::Serializer result; result.Serialize(text); Response(fd, result.GetBuffer());
        }
    });
    auto pool = std::make_shared<kvrpc::ConnectionPool>("127.0.0.1", server.port(), 1);
    std::future<std::string> final;
    {
        kvrpc::RpcClient client(pool);
        CHECK(client.Call<std::string>("echo", "hello").get() == "hello");
        CHECK(client.Call<std::string>("echo", std::string("hello")).get() == "hello");
        final = client.Call<std::string>("echo", "hello");
    }
    CHECK(final.get() == "hello"); server.Finish();
    TestServer oversized([](int fd) {
        Request(fd); const char header[] = {char(255), char(255), char(255), char(255)}; Write(fd, header, 4);
    });
    auto bad_pool = std::make_shared<kvrpc::ConnectionPool>("127.0.0.1", oversized.port(), 1);
    kvrpc::RpcClient bad(bad_pool);
    ErrorIs(kvrpc::ErrorCode::protocol, [&] { bad.Call<int>("bad").get(); }); oversized.Finish();
    TestServer trailing([](int fd) { Request(fd); kvrpc::Serializer s; s.Serialize(int32_t(1), int32_t(2)); Response(fd, s.GetBuffer()); });
    kvrpc::RpcClient strict(std::make_shared<kvrpc::ConnectionPool>("127.0.0.1", trailing.port(), 1));
    ErrorIs(kvrpc::ErrorCode::protocol, [&] { strict.Call<int32_t>("bad").get(); }); trailing.Finish();
    TestServer empty([](int fd) { Request(fd); Response(fd, {}); });
    kvrpc::RpcClient void_client(std::make_shared<kvrpc::ConnectionPool>("127.0.0.1", empty.port(), 1));
    void_client.Call<void>("noop").get(); empty.Finish();
}
