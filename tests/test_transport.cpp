#include "kvrpc/tcp_connection.h"
#include "test_support.h"
using namespace std::chrono_literals;

int main() {
    Throws([&] { kvrpc::TcpConnection connection({0ms, 1ms}); });
    kvrpc::TcpConnection invalid;
    CHECK(!invalid.Connect("not-an-ip", 1));
    TestServer fragmented([](int fd) {
        std::string received(100000, '\0'); Read(fd, received.data(), received.size());
        CHECK(received == std::string(100000, 'x'));
        Write(fd, "hello", 5, true);
    });
    kvrpc::TcpConnection conn;
    CHECK(conn.Connect("127.0.0.1", fragmented.port()));
    std::string data(100000, 'x'); CHECK(conn.SendAll(data.data(), data.size()));
    char response[5]; CHECK(conn.RecvAll(response, 5)); CHECK(std::string(response, 5) == "hello");
    CHECK(!conn.RecvAll(response, 1)); CHECK(!conn.IsConnected()); fragmented.Finish();
    TestServer stalled([](int) { std::this_thread::sleep_for(250ms); });
    kvrpc::TcpConnection timed({100ms, 50ms}); CHECK(timed.Connect("127.0.0.1", stalled.port()));
    auto start = std::chrono::steady_clock::now();
    CHECK(!timed.RecvAll(response, 1)); CHECK(timed.TimedOut()); CHECK(!timed.IsConnected());
    CHECK(std::chrono::steady_clock::now() - start < 1s); stalled.Finish();
    TestServer reset([](int fd) { linger value{1, 0}; setsockopt(fd, SOL_SOCKET, SO_LINGER, &value, sizeof(value)); });
    CHECK(conn.Connect("127.0.0.1", reset.port())); reset.Finish();
    CHECK(!conn.SendAll(data.data(), data.size()) || !conn.SendAll(data.data(), data.size()));
    CHECK(!conn.IsConnected());
    // Progress must not extend the total request deadline indefinitely.
    TestServer trickle([](int fd) {
        for (int i = 0; i < 8; ++i) {
            std::this_thread::sleep_for(50ms);
#ifdef MSG_NOSIGNAL
            const int flags = MSG_NOSIGNAL;
#else
            const int flags = 0;
#endif
            if (send(fd, "x", 1, flags) != 1) break;
        }
    });
    kvrpc::TcpConnection bounded({100ms, 120ms});
    CHECK(bounded.Connect("127.0.0.1", trickle.port()));
    char body[8];
    CHECK(!bounded.RecvAll(body, sizeof(body)) && bounded.TimedOut());
    trickle.Finish();
}
