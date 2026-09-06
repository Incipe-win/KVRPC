#pragma once
#include "protocol.h"
#include <functional>
#include <mutex>
#include <string>

namespace kvcache {
// Synchronous append + fsync. A failed append poisons further writes until restart.
class AofLogger {
public:
    explicit AofLogger(const std::string& filename, size_t max_bytes = 1024ULL * 1024 * 1024);
    ~AofLogger();
    AofLogger(const AofLogger&) = delete;
    AofLogger& operator=(const AofLogger&) = delete;
    void log(Command cmd, const std::string& key, const std::string& value);
    void start();
    void stop();
    using ReplayCallback = std::function<void(Command, const std::string&, const std::string&)>;
    void replay(ReplayCallback callback);
private:
    int fd_ = -1;
    size_t bytes_ = 0;
    size_t max_bytes_;
    bool failed_ = false;
    bool started_ = false;
    std::mutex mutex_;
};
}  // namespace kvcache
