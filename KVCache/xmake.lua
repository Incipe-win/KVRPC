add_rules("mode.debug", "mode.release")

set_languages("c++20")

add_requires("gtest")
add_requires("benchmark")

target("kvcache_lib")

    set_kind("static")

    add_includedirs("include")
    add_files("src/tcp_server.cpp", "src/aof.cpp")


target("kv_server")
    set_kind("binary")
    add_deps("kvcache_lib")
    add_includedirs("include")
    add_files("src/main.cpp")

target("test_lru_cache")
    set_kind("binary")
    add_packages("gtest")
    add_includedirs("include")
    add_files("tests/test_lru_cache.cpp")
    add_tests("default")

target("test_sharded_cache")
    set_kind("binary")
    add_packages("gtest")
    add_includedirs("include")
    add_files("tests/test_sharded_cache.cpp")
    add_tests("default")

target("benchmark_cache")
    set_kind("binary")
    add_packages("benchmark")
    add_includedirs("include")
    add_files("tests/benchmark_cache.cpp")



