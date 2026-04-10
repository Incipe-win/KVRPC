#include <benchmark/benchmark.h>

#include <random>
#include <string>
#include <thread>
#include <vector>

#include "lru_cache.h"
#include "sharded_cache.h"

using namespace kvcache;

// Benchmark Single Lock LRU Cache
static void BM_LRUCache_Put(benchmark::State& state) {
    LRUCache<int, int> cache(10000);
    for (auto _ : state) {
        cache.put(state.range(0), state.range(0));
    }
}

// Benchmark Sharded LRU Cache
static void BM_ShardedCache_Put(benchmark::State& state) {
    ShardedCache<int, int> cache(10000, 16);
    for (auto _ : state) {
        cache.put(state.range(0), state.range(0));
    }
}

// Register Benchmarks
BENCHMARK(BM_LRUCache_Put)->Range(1, 10000);
BENCHMARK(BM_ShardedCache_Put)->Range(1, 10000);

// Concurrent Benchmarks
static void BM_LRUCache_Concurrent(benchmark::State& state) {
    static LRUCache<int, int> cache(100000);
    // Thread local random generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 100000);

    for (auto _ : state) {
        int key = dis(gen);
        cache.put(key, key);
    }
}

static void BM_ShardedCache_Concurrent(benchmark::State& state) {
    static ShardedCache<int, int> cache(100000, 16);
    // Thread local random generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 100000);

    for (auto _ : state) {
        int key = dis(gen);
        cache.put(key, key);
    }
}

BENCHMARK(BM_LRUCache_Concurrent)->Threads(1)->Threads(4)->Threads(8)->Threads(16);
BENCHMARK(BM_ShardedCache_Concurrent)->Threads(1)->Threads(4)->Threads(8)->Threads(16);

BENCHMARK_MAIN();
