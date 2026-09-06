add_rules("mode.debug", "mode.release")
set_languages("c++17")
set_warnings("all", "extra")

target("kvrpc")
    set_kind("static")
    add_includedirs("include", {public = true})
    add_syslinks("pthread", {public = true})
    add_files("src/*.cpp")
    add_headerfiles("include/(kvrpc/*.h)")

for _, name in ipairs({"serializer", "connection_pool", "rpc_client", "kvcache_client", "transport", "executor"}) do
    target("test_" .. name)
        set_kind("binary")
        add_deps("kvrpc")
        add_files("tests/test_" .. name .. ".cpp")
        add_tests("default")
end

target("kvrpc_example")
    set_kind("binary")
    add_deps("kvrpc")
    add_files("examples/kvcache.cpp")

if is_plat("linux") then
    target("kvcache")
        set_kind("static")
        add_deps("kvrpc")
        add_includedirs("KVCache/include", {public = true})
        add_files("KVCache/src/tcp_server.cpp", "KVCache/src/aof.cpp")
    target("kv_server")
        set_kind("binary")
        add_deps("kvcache")
        add_files("KVCache/src/main.cpp")
    target("test_storage")
        set_kind("binary")
        add_deps("kvcache")
        add_files("tests/test_storage.cpp")
        add_tests("default")
end
