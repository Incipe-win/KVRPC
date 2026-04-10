add_rules("mode.debug", "mode.release")

set_languages("c++17")

target("kvrpc")
    set_kind("static")
    add_includedirs("include")
    add_files("src/*.cpp")

target("test_rpc")
    set_kind("binary")
    add_deps("kvrpc")
    add_includedirs("include", "../KVCache/include")
    add_files("tests/*.cpp")
