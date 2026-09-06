#include <kvrpc/rpc_server.h>
#include <csignal>
#include <iostream>
#include <pthread.h>
#include <thread>

int main(int argc, char** argv) {
    try {
        sigset_t signals;
        sigemptyset(&signals); sigaddset(&signals, SIGINT); sigaddset(&signals, SIGTERM);
        if (pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0) throw std::runtime_error("Cannot block signals");
        kvrpc::RpcServer server(argc > 1 ? std::stoi(argv[1]) : 8081);
        server.Register<int64_t, int32_t, int32_t>("add", [](int32_t a, int32_t b) { return int64_t(a) + b; });
        server.Register<std::string, std::string>("echo", [](std::string value) { return value; });
        server.Register<void>("ping", [] {});
        std::thread shutdown([&] {
            int signal = 0;
            if (sigwait(&signals, &signal) == 0) server.Stop();
        });
        try { server.Start(); }
        catch (...) {
            pthread_kill(shutdown.native_handle(), SIGTERM);
            shutdown.join();
            throw;
        }
        shutdown.join();
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
