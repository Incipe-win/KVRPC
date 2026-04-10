#include <iostream>
#include <string>
#include <vector>

#include "aof.h"
#include "protocol.h"
#include "sharded_cache.h"
#include "tcp_server.h"

using namespace kvcache;

std::vector<uint8_t> handle_request(ShardedCache<std::string, std::string>& cache, AofLogger& aof,
                                    const std::vector<uint8_t>& data, size_t& consumed) {
    if (data.size() < HEADER_SIZE) {
        consumed = 0;
        return {};
    }

    Header header = Message::decodeHeader(data.data());

    if (header.magic != MAGIC) {
        consumed = 1;
        return {};
    }

    size_t total_len = HEADER_SIZE + header.key_len + header.value_len;
    if (data.size() < total_len) {
        consumed = 0;
        return {};
    }

    consumed = total_len;

    std::string key(reinterpret_cast<const char*>(data.data() + HEADER_SIZE), header.key_len);
    std::string value;
    if (header.value_len > 0) {
        value.assign(reinterpret_cast<const char*>(data.data() + HEADER_SIZE + header.key_len), header.value_len);
    }

    Command cmd = static_cast<Command>(header.command);
    std::string response_val;
    Command response_cmd = cmd;

    switch (cmd) {
        case Command::SET:
            cache.put(key, value);
            aof.log(cmd, key, value);
            break;
        case Command::GET: {
            auto val = cache.get(key);
            if (val) {
                response_val = *val;
            }
            break;
        }
        case Command::DEL:
            // cache.remove(key);
            // aof.log(cmd, key, "");
            break;
        case Command::STATS: {
            auto stats = cache.getStats();
            response_val = "Hits: " + std::to_string(stats.hits) + ", Misses: " + std::to_string(stats.misses);
            break;
        }
        default:
            break;
    }

    return Message::encode(response_cmd, key, response_val);
}

int main(int argc, char** argv) {
    int port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    std::cout << "Initializing Sharded Cache..." << std::endl;
    ShardedCache<std::string, std::string> cache(1000, 16);

    std::cout << "Initializing AOF..." << std::endl;
    AofLogger aof("appendonly.aof");

    std::cout << "Replaying AOF..." << std::endl;
    aof.replay([&cache](Command cmd, const std::string& key, const std::string& value) {
        if (cmd == Command::SET) {
            cache.put(key, value);
        } else if (cmd == Command::DEL) {
            // cache.remove(key);
        }
    });

    aof.start();

    std::cout << "Starting Server on port " << port << "..." << std::endl;
    TcpServer server(port);

    server.setHandler([&cache, &aof](const std::vector<uint8_t>& data, size_t& consumed) {
        return handle_request(cache, aof, data, consumed);
    });

    try {
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
