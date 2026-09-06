#include "cache_service.h"
#include "tcp_server.h"
#include <csignal>
#include <iostream>
#include <pthread.h>
#include <thread>

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--help") {
        std::cout << "Usage: kv_server [port=8080] [aof=appendonly.aof] [bind=127.0.0.1] [sync=group|always]\n";
        return 0;
    }
    try {
        if (argc > 5) throw std::invalid_argument("Too many arguments; use --help");
        size_t parsed = 0;
        const std::string port_text = argc > 1 ? argv[1] : "8080";
        int port = std::stoi(port_text, &parsed);
        if (parsed != port_text.size() || port < 1 || port > 65535) throw std::invalid_argument("Invalid port");
        sigset_t signals;
        sigemptyset(&signals); sigaddset(&signals, SIGINT); sigaddset(&signals, SIGTERM);
        if (pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0) throw std::runtime_error("Cannot block signals");
        kvcache::CacheService service(1000, 16, argc > 2 ? argv[2] : "appendonly.aof");
        const std::string sync = argc > 4 ? argv[4] : "group";
        if (sync != "group" && sync != "always") throw std::invalid_argument("Sync mode must be group or always");
        kvcache::TcpServer server(port, argc > 3 ? argv[3] : "127.0.0.1", 128, kvcache::MAX_FRAME_SIZE);
        if (sync == "group") server.setBatchHandler([&](const std::vector<kvcache::TcpServer::Input>& inputs) { return service.HandleBatch(inputs); });
        else server.setHandler([&](const std::vector<uint8_t>& bytes, size_t& consumed) { return service.Handle(bytes, consumed); });
        std::atomic<bool> done{false};
        std::thread shutdown([&] {
            while (!done) {
                timespec timeout{0, 100000000};
                int signal = sigtimedwait(&signals, nullptr, &timeout);
                if (signal == SIGINT || signal == SIGTERM) { server.stop(); return; }
            }
        });
        try { server.start(); }
        catch (...) { done = true; shutdown.join(); throw; }
        done = true; shutdown.join();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "kv_server: " << e.what() << '\n';
        return 1;
    }
}
