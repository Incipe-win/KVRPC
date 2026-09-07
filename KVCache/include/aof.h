#pragma once
#include <functional>
#include <mutex>
#include <string>

#include "protocol.h"
namespace kvcache {
class AofCapacityError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
class AofLogger {
public:
    explicit AofLogger(const std::string& filename, size_t max_bytes = 1024ULL * 1024 * 1024);
    ~AofLogger();
    AofLogger(const AofLogger&) = delete;
    AofLogger& operator=(const AofLogger&) = delete;
    struct Mutation {
        Command command;
        std::string key, value;
    };
    struct Stats {
        uint64_t records = 0, syncs = 0, bytes = 0, rewrites = 0, sync_us = 0;
    };
    void log(Command cmd, const std::string& key, const std::string& value);
    void logBatch(const std::vector<Mutation>& mutations);
    bool needsRewrite(const std::vector<Mutation>& mutations) const;
    void rewrite(const std::vector<Mutation>& snapshot);
    Stats stats() const;
    void start();
    void stop();
    using ReplayCallback = std::function<void(Command, const std::string&, const std::string&)>;
    void replay(ReplayCallback callback);

private:
    int fd_ = -1, lock_ = -1;
    std::string filename_;
    size_t bytes_ = 0, rewritten_at_ = 0, max_bytes_;
    bool failed_ = false, started_ = false;
    mutable std::mutex mutex_;
    Stats stats_;
};
}  // namespace kvcache
