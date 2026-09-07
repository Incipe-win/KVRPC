# Contributing

Linux, a C++17 compiler, CMake, Python 3, and Protobuf compiler/development packages are required. Use the repository .clang-format.

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel 4
ctest --test-dir build/release --output-on-failure --parallel 4
cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=Debug -DKVRPC_SANITIZE=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build/asan --parallel 4
ctest --test-dir build/asan --output-on-failure --parallel 4
cmake -S . -B build/tsan -DCMAKE_BUILD_TYPE=Debug -DKVRPC_TSAN=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build/tsan --parallel 4
ctest --test-dir build/tsan --output-on-failure --parallel 4
```

Run networking tests with local socket permissions. Register callbacks before starting; keep all socket state on its owning EventLoop. Changes to ET budgets must retain explicit continuations. Changes to asynchronous execution must preserve bounded admission, ownership, and request correlation. Changes to storage must preserve fsync-before-ack and crash-safe replacement.

Use CMake installation plus tests/install to validate public headers and transitive Protobuf linkage. CI covers release/debug/sanitizers and Docker. Xmake is an alternative; do not claim it was tested unless executed.

Keep old benchmark data attributed to its original binary. Generate new files for new versions and report workload, arrival model, errors, and hardware alongside latency/throughput.
