#include <pthread.h>

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "cache_service.h"
#include "tcp_server.h"

namespace {
size_t Setting(const char* name, size_t fallback) {
    const char* value = std::getenv(name);
    if (!value) return fallback;
    std::string text(value);
    size_t used = 0;
    if (text.empty() || text[0] == '-') throw std::invalid_argument(name);
    auto result = std::stoull(text, &used);
    if (!result || used != text.size()) throw std::invalid_argument(name);
    return result;
}
}  // namespace

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
        sigemptyset(&signals);
        sigaddset(&signals, SIGINT);
        sigaddset(&signals, SIGTERM);
        if (pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0) throw std::runtime_error("Cannot block signals");
        kvcache::CacheService service(Setting("KVRPC_CACHE_ENTRIES", 1000), Setting("KVRPC_CACHE_SHARDS", 16),
                                      argc > 2 ? argv[2] : "appendonly.aof",
                                      Setting("KVRPC_AOF_BYTES", 1024ULL * 1024 * 1024),
                                      Setting("KVRPC_CACHE_BYTES", 64 * 1024 * 1024));
        const std::string sync = argc > 4 ? argv[4] : "group";
        if (sync != "group" && sync != "always") throw std::invalid_argument("Sync mode must be group or always");
        kvcache::TcpServer server(port, argc > 3 ? argv[3] : "127.0.0.1", Setting("KVRPC_CONNECTIONS", 4096),
                                  kvcache::MAX_FRAME_SIZE);
        kvrpc::ServerOptions options;
        options.io_threads = Setting("KVRPC_IO_THREADS", 2);
        options.workers = 1;
        options.pipeline = 1;
        options.queue_capacity = Setting("KVRPC_QUEUE_CAPACITY", 1024);
        options.queue_bytes = Setting("KVRPC_QUEUE_BYTES", 64 * 1024 * 1024);
        options.output_bytes = Setting("KVRPC_OUTPUT_BYTES", 8 * 1024 * 1024);
        options.idle_timeout = std::chrono::milliseconds(Setting("KVRPC_IDLE_MS", 30000));
        server.configure(options);
        service.SetTransportMetrics([&] {
            auto s = server.stats();
            return ", Active connections: " + std::to_string(s.active) + ", Rejected: " + std::to_string(s.rejected) +
                   ", Queued tasks: " + std::to_string(s.queued_tasks) +
                   ", Queued bytes: " + std::to_string(s.queued_bytes) + ", Timeouts: " + std::to_string(s.timeouts) +
                   ", Handler us: " + std::to_string(s.handler_us) +
                   ", Handler errors: " + std::to_string(s.handler_errors);
        });
        server.setFrameSizer([](const uint8_t* bytes, size_t size) -> size_t {
            if (size < kvcache::HEADER_SIZE) return 0;
            auto h = kvcache::Message::decodeHeader(bytes);
            kvcache::Message::validate(h);
            return kvcache::HEADER_SIZE + size_t(h.key_len) + h.value_len;
        });
        if (sync == "group")
            server.setBatchHandler(
                [&](const std::vector<kvcache::TcpServer::Input>& inputs) { return service.HandleBatch(inputs); });
        else
            server.setHandler(
                [&](const std::vector<uint8_t>& bytes, size_t& consumed) { return service.Handle(bytes, consumed); });
        std::atomic<bool> done{false};
        std::thread shutdown([&] {
            while (!done) {
                timespec timeout{0, 100000000};
                int signal = sigtimedwait(&signals, nullptr, &timeout);
                if (signal == SIGINT || signal == SIGTERM) {
                    server.stop();
                    return;
                }
            }
        });
        std::cout << "{\"event\":\"starting\",\"port\":" << port << ",\"io_threads\":" << options.io_threads << "}\n";
        try {
            server.start();
        } catch (...) {
            done = true;
            shutdown.join();
            throw;
        }
        done = true;
        shutdown.join();
        auto stats = server.stats();
        std::cout << "{\"event\":\"stopped\",\"accepted\":" << stats.accepted << ",\"requests\":" << stats.requests
                  << ",\"rejected\":" << stats.rejected << ",\"timeouts\":" << stats.timeouts << "}\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "kv_server: " << e.what() << '\n';
        return 1;
    }
}
