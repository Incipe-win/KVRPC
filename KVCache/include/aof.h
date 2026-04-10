#pragma once

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "protocol.h"

namespace kvcache {

class AofLogger {
public:
    AofLogger(const std::string& filename, int interval_ms = 1000);
    ~AofLogger();

    void log(Command cmd, const std::string& key, const std::string& value);
    void start();
    void stop();

    // Replay function to restore state
    using ReplayCallback = std::function<void(Command, const std::string&, const std::string&)>;
    void replay(ReplayCallback callback);

private:
    std::string filename_;
    int interval_ms_;
    std::atomic<bool> running_;
    std::thread flush_thread_;

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<std::vector<uint8_t>> queue_;

    void flushLoop();
};

}  // namespace kvcache
