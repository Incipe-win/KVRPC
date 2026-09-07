#include "aof.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <filesystem>
namespace kvcache {
namespace {
void ReadExact(int fd, void* buffer, size_t size) {
    auto* data = static_cast<char*>(buffer);
    while (size) {
        auto n = read(fd, data, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("Truncated AOF record");
        data += n;
        size -= n;
    }
}
void WriteExact(int fd, const std::vector<uint8_t>& data) {
    size_t offset = 0;
    while (offset < data.size()) {
        auto n = write(fd, data.data() + offset, data.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("AOF write failed");
        offset += n;
    }
}
void Sync(int fd) {
    int result;
    do {
        result = fsync(fd);
    } while (result < 0 && errno == EINTR);
    if (result < 0) throw std::runtime_error("AOF fsync failed");
}
void SyncDirectory(const std::string& filename) {
    auto path = std::filesystem::path(filename).parent_path();
    int fd = open(path.empty() ? "." : path.c_str(), O_DIRECTORY | O_RDONLY | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error("Cannot open AOF directory");
    try {
        Sync(fd);
    } catch (...) {
        close(fd);
        throw;
    }
    close(fd);
}
uint32_t Crc(const uint8_t* data, size_t size) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
            t[i] = c;
        }
        return t;
    }();
    uint32_t crc = ~0u;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 255] ^ (crc >> 8);
    return ~crc;
}
std::vector<uint8_t> Encode(const AofLogger::Mutation& m) {
    if ((m.command != Command::SET && m.command != Command::DEL && m.command != Command::SET_TTL) ||
        (m.command == Command::DEL && !m.value.empty()))
        throw std::invalid_argument("Invalid AOF mutation");
    auto payload = Message::encode(m.command, m.key, m.value);
    std::vector<uint8_t> result(12);
    std::memcpy(result.data(), "AOF2", 4);
    uint32_t size = htonl(payload.size()), crc = htonl(Crc(payload.data(), payload.size()));
    std::memcpy(result.data() + 4, &size, 4);
    std::memcpy(result.data() + 8, &crc, 4);
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}
size_t Size(const std::vector<AofLogger::Mutation>& mutations) {
    size_t result = 0;
    for (const auto& m : mutations) result += Encode(m).size();
    return result;
}
}  // namespace
AofLogger::AofLogger(const std::string& filename, size_t max_bytes) : filename_(filename), max_bytes_(max_bytes) {
    lock_ = open((filename + ".lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock_ < 0) throw std::runtime_error("Cannot open AOF lock");
    try {
        if (flock(lock_, LOCK_EX | LOCK_NB) < 0) throw std::runtime_error("AOF already in use");
        fd_ = open(filename.c_str(), O_CREAT | O_RDWR | O_APPEND | O_CLOEXEC, 0600);
        if (fd_ < 0) throw std::runtime_error("Cannot open AOF");
        if (flock(fd_, LOCK_EX | LOCK_NB) < 0) throw std::runtime_error("AOF already in use");
        struct stat info{};
        if (fstat(fd_, &info) || !S_ISREG(info.st_mode) || info.st_size < 0 || uint64_t(info.st_size) > max_bytes_)
            throw std::runtime_error("Invalid AOF file or size limit");
        bytes_ = info.st_size;
        stats_.bytes = bytes_;
        SyncDirectory(filename);
    } catch (...) {
        if (fd_ >= 0) close(fd_);
        close(lock_);
        throw;
    }
}
AofLogger::~AofLogger() {
    if (fd_ >= 0) close(fd_);
    if (lock_ >= 0) close(lock_);
}
void AofLogger::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    started_ = true;
}
void AofLogger::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    started_ = false;
}
AofLogger::Stats AofLogger::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}
void AofLogger::log(Command command, const std::string& key, const std::string& value) {
    logBatch({{command, key, value}});
}
bool AofLogger::needsRewrite(const std::vector<Mutation>& mutations) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mutations.empty()) return false;
    return Size(mutations) > max_bytes_ - bytes_ ||
           (bytes_ > max_bytes_ / 2 && bytes_ - rewritten_at_ > max_bytes_ / 4);
}
void AofLogger::logBatch(const std::vector<Mutation>& mutations) {
    if (mutations.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_ || failed_) throw std::runtime_error("AOF unavailable");
    auto size = Size(mutations);
    if (size > max_bytes_ - bytes_) throw AofCapacityError("AOF capacity exceeded after compaction");
    try {
        for (const auto& mutation : mutations) WriteExact(fd_, Encode(mutation));
        auto start = std::chrono::steady_clock::now();
        Sync(fd_);
        stats_.sync_us +=
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    } catch (...) {
        failed_ = true;
        throw;
    }
    bytes_ += size;
    stats_.bytes = bytes_;
    stats_.records += mutations.size();
    ++stats_.syncs;
}
void AofLogger::rewrite(const std::vector<Mutation>& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (failed_) throw std::runtime_error("AOF unavailable");
    size_t size = Size(snapshot);
    if (size > max_bytes_) throw AofCapacityError("Snapshot exceeds AOF limit");
    std::string path = filename_ + ".rewrite-XXXXXX";
    int replacement = mkstemp(path.data());
    if (replacement < 0) throw std::runtime_error("Cannot create AOF rewrite");
    bool renamed = false;
    try {
        fcntl(replacement, F_SETFD, FD_CLOEXEC);
        if (flock(replacement, LOCK_EX | LOCK_NB)) throw std::runtime_error("Cannot lock AOF replacement");
        for (const auto& entry : snapshot) WriteExact(replacement, Encode(entry));
        Sync(replacement);
        if (rename(path.c_str(), filename_.c_str())) throw std::runtime_error("Cannot publish AOF rewrite");
        renamed = true;
        close(fd_);
        fd_ = replacement;
        replacement = -1;
        bytes_ = size;
        rewritten_at_ = size;
        stats_.bytes = size;
        ++stats_.rewrites;
        SyncDirectory(filename_);
    } catch (...) {
        if (replacement >= 0) close(replacement);
        if (!renamed)
            unlink(path.c_str());
        else
            failed_ = true;
        throw;
    }
}
void AofLogger::replay(ReplayCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_) throw std::logic_error("Replay must precede start");
    if (lseek(fd_, 0, SEEK_SET) < 0) throw std::runtime_error("Cannot seek AOF");
    size_t remaining = bytes_;
    while (remaining) {
        if (remaining < HEADER_SIZE) throw std::runtime_error("Truncated AOF header");
        uint8_t header[12];
        ReadExact(fd_, header, sizeof(header));
        remaining -= sizeof(header);
        std::vector<uint8_t> payload;
        if (!std::memcmp(header, "AOF2", 4)) {
            uint32_t length, crc;
            std::memcpy(&length, header + 4, 4);
            std::memcpy(&crc, header + 8, 4);
            length = ntohl(length);
            if (length < HEADER_SIZE || length > MAX_FRAME_SIZE || length > remaining)
                throw std::runtime_error("Invalid AOF record length");
            payload.resize(length);
            ReadExact(fd_, payload.data(), length);
            remaining -= length;
            if (Crc(payload.data(), payload.size()) != ntohl(crc)) throw std::runtime_error("AOF checksum mismatch");
        } else {
            // Read compatibility for existing version-1 logs; all new records are checksummed.
            auto h = Message::decodeHeader(header);
            Message::validate(h);
            size_t length = size_t(h.key_len) + h.value_len;
            if (length > remaining) throw std::runtime_error("Truncated legacy AOF");
            payload.assign(header, header + 12);
            payload.resize(12 + length);
            ReadExact(fd_, payload.data() + 12, length);
            remaining -= length;
        }
        auto h = Message::decodeHeader(payload.data());
        Message::validate(h);
        if (HEADER_SIZE + size_t(h.key_len) + h.value_len != payload.size() ||
            (h.command != uint8_t(Command::SET) && h.command != uint8_t(Command::DEL) &&
             h.command != uint8_t(Command::SET_TTL)) ||
            (h.command == uint8_t(Command::DEL) && h.value_len))
            throw std::runtime_error("Invalid AOF operation");
        callback(static_cast<Command>(h.command),
                 std::string(reinterpret_cast<char*>(payload.data() + HEADER_SIZE), h.key_len),
                 std::string(reinterpret_cast<char*>(payload.data() + HEADER_SIZE + h.key_len), h.value_len));
    }
}
}  // namespace kvcache
