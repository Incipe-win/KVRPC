#include <gtest/gtest.h>

#include <string>

#include "lru_cache.h"

using namespace kvcache;

TEST(LRUCacheTest, BasicPutGet) {
    LRUCache<std::string, int> cache(3);
    cache.put("one", 1);
    cache.put("two", 2);

    auto val = cache.get("one");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 1);

    EXPECT_FALSE(cache.get("three").has_value());
}

TEST(LRUCacheTest, EvictionPolicy) {
    LRUCache<int, int> cache(2);
    cache.put(1, 10);
    cache.put(2, 20);

    // Access 1 to make it recently used
    cache.get(1);

    // Add 3, should evict 2 (LRU)
    cache.put(3, 30);

    EXPECT_TRUE(cache.get(1).has_value());
    EXPECT_TRUE(cache.get(3).has_value());
    EXPECT_FALSE(cache.get(2).has_value());
}

TEST(LRUCacheTest, UpdateValue) {
    LRUCache<int, int> cache(2);
    cache.put(1, 10);
    cache.put(1, 20);  // Update

    EXPECT_EQ(cache.get(1).value(), 20);
    EXPECT_EQ(cache.size(), 1);
}
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}