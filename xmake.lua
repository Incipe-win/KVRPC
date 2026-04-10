add_rules("mode.debug", "mode.release")

set_languages("c++17")

target("kvrpc")
    set_kind("static")
    add_includedirs("include")
    add_files("src/*.cpp")

target("test_serializer")
    set_kind("binary")
    add_deps("kvrpc")
    add_includedirs("include", "../KVCache/include")
    add_files("tests/test_serializer.cpp")

target("test_connection_pool")
    set_kind("binary")
    add_deps("kvrpc")
    add_includedirs("include", "../KVCache/include")
    add_files("tests/test_connection_pool.cpp")

target("test_rpc_client")
    set_kind("binary")
    add_deps("kvrpc")
    add_includedirs("include", "../KVCache/include")
    add_files("tests/test_rpc_client.cpp")

target("test_kvcache_client")
    set_kind("binary")
    add_deps("kvrpc")
    add_includedirs("include", "../KVCache/include")
    add_files("tests/test_kvcache_client.cpp")
