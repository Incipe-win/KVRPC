#include "aof.h"
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace kvcache {
namespace {
void ReadExact(int fd, char* data, size_t size) {
    while (size) {
        auto n = read(fd, data, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("AOF is truncated or unreadable; restore a verified backup");
        data += n; size -= static_cast<size_t>(n);
    }
}
}
AofLogger::AofLogger(const std::string& filename, size_t max_bytes) : max_bytes_(max_bytes) {
    fd_ = open(filename.c_str(), O_CREAT | O_RDWR | O_APPEND | O_CLOEXEC, 0600);
    if (fd_ < 0) throw std::runtime_error("Cannot open AOF");
    try {
        if (flock(fd_, LOCK_EX | LOCK_NB) < 0) throw std::runtime_error("AOF is locked by another process");
        struct stat info{};
        if (fstat(fd_, &info) < 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
            static_cast<uint64_t>(info.st_size) > max_bytes_)
            throw std::runtime_error("Invalid AOF type or size limit exceeded");
        bytes_ = static_cast<size_t>(info.st_size);
        // Persist the directory entry as well as subsequent file contents.
        auto parent = std::filesystem::path(filename).parent_path();
        int directory = open(parent.empty() ? "." : parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (directory < 0) throw std::runtime_error("Cannot open AOF directory");
        int result = fsync(directory);
        close(directory);
        if (result < 0) throw std::runtime_error("Cannot sync AOF directory");
    } catch (...) { close(fd_); fd_ = -1; throw; }
}
AofLogger::~AofLogger() { if (fd_ >= 0) close(fd_); }
void AofLogger::start() { std::lock_guard<std::mutex> lock(mutex_); started_ = true; }
void AofLogger::stop() { std::lock_guard<std::mutex> lock(mutex_); started_ = false; }
void AofLogger::log(Command cmd, const std::string& key, const std::string& value) {
    logBatch({{cmd, key, value}});
}
AofLogger::Stats AofLogger::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}
void AofLogger::logBatch(const std::vector<Mutation>& mutations) {
    if (mutations.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_ || failed_) throw std::runtime_error("AOF is stopped or failed");
    // Preflight the whole batch: capacity rejection must not write a prefix.
    size_t total = 0;
    for (const auto& mutation : mutations) {
        if (mutation.command != Command::SET && mutation.command != Command::DEL)
            throw std::invalid_argument("Invalid AOF command");
        if (mutation.command == Command::DEL && !mutation.value.empty())
            throw std::invalid_argument("DEL cannot carry a value");
        auto encoded = Message::encode(mutation.command, mutation.key, mutation.value);
        if (encoded.size() > max_bytes_ - bytes_ - total) throw std::runtime_error("AOF size limit reached");
        total += encoded.size();
    }
    try {
        for (const auto& mutation : mutations) {
            auto data = Message::encode(mutation.command, mutation.key, mutation.value);
            size_t offset = 0;
            while (offset < data.size()) {
                auto n = write(fd_, data.data() + offset, data.size() - offset);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) throw std::runtime_error("AOF write failed");
                offset += static_cast<size_t>(n);
            }
        }
        int result;
        do { result = fsync(fd_); } while (result < 0 && errno == EINTR);
        if (result < 0) throw std::runtime_error("AOF sync failed");
    } catch (...) { failed_ = true; throw; }
    bytes_ += total;
    stats_.records += mutations.size();
    ++stats_.syncs;
    stats_.bytes += total;
}
void AofLogger::replay(ReplayCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_) throw std::logic_error("Replay must precede start");
    if (lseek(fd_, 0, SEEK_SET) < 0) throw std::runtime_error("Cannot seek AOF");
    size_t remaining = bytes_;
    while (remaining) {
        if (remaining < HEADER_SIZE) throw std::runtime_error("Truncated AOF header");
        uint8_t bytes[HEADER_SIZE];
        ReadExact(fd_, reinterpret_cast<char*>(bytes), HEADER_SIZE);
        auto h = Message::decodeHeader(bytes);
        Message::validate(h);
        if ((h.command != static_cast<uint8_t>(Command::SET) && h.command != static_cast<uint8_t>(Command::DEL)) ||
            (h.command == static_cast<uint8_t>(Command::DEL) && h.value_len))
            throw std::runtime_error("Invalid AOF operation");
        remaining -= HEADER_SIZE;
        size_t body = size_t(h.key_len) + h.value_len;
        if (body > remaining) throw std::runtime_error("Truncated AOF body");
        std::string key(h.key_len, '\0'), value(h.value_len, '\0');
        ReadExact(fd_, key.data(), key.size()); ReadExact(fd_, value.data(), value.size());
        callback(static_cast<Command>(h.command), key, value);
        remaining -= body;
    }
}
}  // namespace kvcache
