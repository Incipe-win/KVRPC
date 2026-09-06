#include <kvrpc/kvcache_client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>

using Clock = std::chrono::steady_clock;
int main(int argc, char** argv) {
    try {
        if (argc != 6) throw std::invalid_argument("Usage: kv_bench port concurrency seconds write_percent value_bytes");
        int port = std::stoi(argv[1]), concurrency = std::stoi(argv[2]), write_percent = std::stoi(argv[4]);
        double seconds = std::stod(argv[3]);
        size_t value_bytes = std::stoul(argv[5]);
        if (port < 1 || port > 65535 || concurrency < 1 || concurrency > 128 || seconds <= 0 || seconds > 3600 ||
            write_percent < 0 || write_percent > 100 || value_bytes > kvcache::MAX_VALUE_SIZE)
            throw std::invalid_argument("Invalid benchmark arguments");
        std::string value(value_bytes, 'x');
        auto make_pool = [&] { return std::make_shared<kvrpc::ConnectionPool>("127.0.0.1", port, 1); };
        kvrpc::KVCacheClient control(make_pool());
        for (int i = 0; i < concurrency * 4; ++i) control.Set("bench:" + std::to_string(i), value).get();
        const auto before = control.Stats().get();
        std::mutex mutex;
        std::condition_variable ready;
        int waiting = 0;
        bool go = false;
        Clock::time_point start;
        std::vector<std::vector<double>> latencies(concurrency);
        std::vector<uint64_t> errors(concurrency), completed(concurrency), writes(concurrency);
        std::vector<std::thread> threads;
        std::vector<std::exception_ptr> startup_errors(concurrency);
        for (int worker = 0; worker < concurrency; ++worker) threads.emplace_back([&, worker] {
            std::unique_ptr<kvrpc::KVCacheClient> owned;
            try {
                owned = std::make_unique<kvrpc::KVCacheClient>(make_pool(), kvrpc::ClientOptions{1, 1});
                // Establish and warm this connection outside the measured interval.
                owned->Get("bench:" + std::to_string(worker * 4)).get();
            } catch (...) { startup_errors[worker] = std::current_exception(); }
            {
                std::unique_lock<std::mutex> lock(mutex);
                ++waiting; ready.notify_all(); ready.wait(lock, [&] { return go; });
            }
            if (startup_errors[worker]) return;
            auto& client = *owned;
            uint32_t random = 0x12345678u + worker;
            uint64_t sequence = 0;
            while (std::chrono::duration<double>(Clock::now() - start).count() < seconds) {
                random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                bool write = random % 100 < static_cast<uint32_t>(write_percent);
                auto key = "bench:" + std::to_string(worker * 4 + sequence++ % 4);
                auto begin = Clock::now();
                try {
                    if (write) { if (!client.Set(key, value).get()) throw std::runtime_error("SET not acknowledged"); ++writes[worker]; }
                    else if (client.Get(key).get() != value) throw std::runtime_error("GET mismatch");
                    ++completed[worker];
                    latencies[worker].push_back(std::chrono::duration<double, std::micro>(Clock::now() - begin).count());
                } catch (...) { ++errors[worker]; }
            }
        });
        {
            std::unique_lock<std::mutex> lock(mutex);
            ready.wait(lock, [&] { return waiting == concurrency; });
            start = Clock::now(); go = true;
        }
        ready.notify_all();
        for (auto& thread : threads) thread.join();
        for (const auto& error : startup_errors) if (error) std::rethrow_exception(error);
        double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
        std::vector<double> all;
        uint64_t count = 0, failed = 0, mutations = 0;
        for (int i = 0; i < concurrency; ++i) {
            all.insert(all.end(), latencies[i].begin(), latencies[i].end());
            count += completed[i]; failed += errors[i]; mutations += writes[i];
        }
        std::sort(all.begin(), all.end());
        auto percentile = [&](double p) { return all.empty() ? 0.0 : all[static_cast<size_t>((all.size() - 1) * p)]; };
        auto counter = [](const std::string& stats, const std::string& name) {
            auto offset = stats.find(name + ": ");
            if (offset == std::string::npos) throw std::runtime_error("Missing server counter");
            return std::stoull(stats.substr(offset + name.size() + 2));
        };
        const auto after = control.Stats().get();
        auto syncs = counter(after, "AOF syncs") - counter(before, "AOF syncs");
        auto records = counter(after, "AOF records") - counter(before, "AOF records");
        std::cout << std::fixed << std::setprecision(3)
            << "{\"concurrency\":" << concurrency << ",\"write_percent\":" << write_percent
            << ",\"value_bytes\":" << value_bytes << ",\"elapsed_seconds\":" << elapsed
            << ",\"completed\":" << count << ",\"errors\":" << failed << ",\"writes\":" << mutations
            << ",\"ops_per_second\":" << count / elapsed << ",\"p50_us\":" << percentile(.50)
            << ",\"p95_us\":" << percentile(.95) << ",\"p99_us\":" << percentile(.99)
            << ",\"max_us\":" << percentile(1) << ",\"aof_syncs\":" << syncs << ",\"aof_records\":" << records << "}\n";
        return failed || records != mutations ? 1 : 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
