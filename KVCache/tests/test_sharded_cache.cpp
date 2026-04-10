#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

#include "sharded_cache.h"

using namespace kvcache;

TEST(ShardedCacheTest, BasicPutGet) {
    ShardedCache<std::string, int> cache(100, 4);
    cache.put("key1", 100);
    auto val = cache.get("key1");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 100);
}

TEST(ShardedCacheTest, Distribution) {
    ShardedCache<int, int> cache(100, 4);
    for (int i = 0; i < 20; ++i) {
        cache.put(i, i * 10);
    }

    int found_count = 0;
    for (int i = 0; i < 20; ++i) {
        if (cache.get(i).has_value()) found_count++;
    }
    EXPECT_EQ(found_count, 20);
}

TEST(ShardedCacheTest, Concurrency) {
    ShardedCache<int, int> cache(1000, 16);
    std::vector<std::thread> threads;

    // 10 threads writing
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&cache, i]() {
            for (int j = 0; j < 100; ++j) {
                cache.put(i * 100 + j, j);
            }
        });
    }

    for (auto& t : threads) t.join();

    EXPECT_EQ(cache.size(), 1000);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
