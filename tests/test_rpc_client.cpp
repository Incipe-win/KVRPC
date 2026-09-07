#include "kvrpc/rpc_client.h"
#include "test_support.h"
using namespace kvrpc;
std::pair<uint64_t, wire::Envelope> Request(int fd) {
    std::vector<uint8_t> bytes(RPC_HEADER);
    Read(fd, reinterpret_cast<char*>(bytes.data()), bytes.size());
    auto size = RpcFrameSize(bytes.data(), bytes.size());
    CHECK(size < 4096);
    bytes.resize(size);
    Read(fd, reinterpret_cast<char*>(bytes.data() + RPC_HEADER), size - RPC_HEADER);
    return {RpcId(bytes), RpcDecode(bytes)};
}
void Response(int fd, uint64_t id, wire::Envelope result) {
    result.set_type(wire::Envelope::RESPONSE);
    auto bytes = RpcEncode(id, result, 4096);
    Write(fd, reinterpret_cast<char*>(bytes.data()), bytes.size(), true);
}
int main() {
    TestServer server([](int fd) {
        for (int i = 0; i < 3; ++i) {
            auto [id, request] = Request(fd);
            CHECK(request.method() == "echo");
            wire::Envelope response;
            *response.mutable_result() = request.arguments(0);
            Response(fd, id, response);
        }
    });
    std::future<std::string> final;
    {
        RpcClient client(std::make_shared<ConnectionPool>("127.0.0.1", server.port(), 1));
        CHECK(client.Call<std::string>("echo", "hello").get() == "hello");
        CHECK(client.Call<std::string>("echo", std::string("hello")).get() == "hello");
        final = client.Call<std::string>("echo", "hello");
    }
    CHECK(final.get() == "hello");
    server.Finish();
    TestServer wrong_id([](int fd) {
        auto [id, request] = Request(fd);
        Response(fd, id + 1, {});
    });
    RpcClient bad(std::make_shared<ConnectionPool>("127.0.0.1", wrong_id.port(), 1));
    ErrorIs(ErrorCode::protocol, [&] { bad.Call<void>("bad").get(); });
    wrong_id.Finish();
    TestServer wrong_type([](int fd) {
        auto [id, request] = Request(fd);
        wire::Envelope result;
        result.mutable_result()->set_string_value("x");
        Response(fd, id, result);
    });
    RpcClient strict(std::make_shared<ConnectionPool>("127.0.0.1", wrong_type.port(), 1));
    ErrorIs(ErrorCode::protocol, [&] { strict.Call<int32_t>("bad").get(); });
    wrong_type.Finish();
}
