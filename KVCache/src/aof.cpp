#include "aof.h"

#include <filesystem>
#include <iostream>

namespace kvcache {

AofLogger::AofLogger(const std::string& filename, int interval_ms)
    : filename_(filename), interval_ms_(interval_ms), running_(false) {
}

AofLogger::~AofLogger() {
    stop();
}

void AofLogger::start() {
    running_ = true;
    flush_thread_ = std::thread(&AofLogger::flushLoop, this);
}

void AofLogger::stop() {
    if (!running_) return;
    running_ = false;
    queue_cv_.notify_all();
    if (flush_thread_.joinable()) {
        flush_thread_.join();
    }
}

void AofLogger::log(Command cmd, const std::string& key, const std::string& value) {
    auto data = Message::encode(cmd, key, value);
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push(std::move(data));
    }
    queue_cv_.notify_one();
}

void AofLogger::flushLoop() {
    std::ofstream outfile;
    outfile.open(filename_, std::ios::app | std::ios::binary);

    while (running_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (queue_cv_.wait_for(lock, std::chrono::milliseconds(interval_ms_),
                               [this] { return !queue_.empty() || !running_; })) {
            while (!queue_.empty()) {
                auto& data = queue_.front();
                outfile.write(reinterpret_cast<const char*>(data.data()), data.size());
                queue_.pop();
            }
            outfile.flush();
        }
    }

    // Flush remaining
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!queue_.empty()) {
            auto& data = queue_.front();
            outfile.write(reinterpret_cast<const char*>(data.data()), data.size());
            queue_.pop();
        }
        outfile.flush();
    }
    outfile.close();
}

void AofLogger::replay(ReplayCallback callback) {
    if (!std::filesystem::exists(filename_)) return;

    std::ifstream infile(filename_, std::ios::binary);
    if (!infile.is_open()) return;

    std::vector<uint8_t> buffer;
    // Read file in chunks or message by message
    // Since our protocol has length, we can read header then body.

    while (infile.peek() != EOF) {
        Header header;
        if (!infile.read(reinterpret_cast<char*>(&header), HEADER_SIZE)) break;

        // Convert from network byte order
        header.magic = ntohs(header.magic);
        header.key_len = ntohl(header.key_len);
        header.value_len = ntohl(header.value_len);

        if (header.magic != MAGIC) {
            std::cerr << "AOF Corrupted: Invalid Magic" << std::endl;
            break;
        }

        std::string key(header.key_len, '\0');
        std::string value(header.value_len, '\0');

        if (header.key_len > 0) infile.read(&key[0], header.key_len);
        if (header.value_len > 0) infile.read(&value[0], header.value_len);

        callback(static_cast<Command>(header.command), key, value);
    }
}

}  // namespace kvcache
