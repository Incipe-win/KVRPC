# TSan exception-runtime instrumentation

Unmodified files from GCC's `releases/gcc-14.2.0` tag:
https://github.com/gcc-mirror/gcc/tree/releases/gcc-14.2.0/libstdc%2B%2B-v3/libsupc%2B%2B

Only KVRPC_TSAN builds compile these sources. Production/ASan builds continue to use the system C++ ABI runtime unchanged. No race suppressions or ignored tests are used.

The distribution libstdc++.so executes exception reference-count atomics without TSan instrumentation. Consequently, TSan can miss the release/acquire relationship between a catch handler finishing in one thread and the last exception_ptr being released in another. This appears as free-versus-read races in otherwise valid promise/future exception transfer.

GCC documents rebuilding affected runtime source files and ELF symbol interposition as a way to expose library synchronization:
https://gcc.gnu.org/onlinedocs/libstdc++/manual/debug.html#debug.races

We compile eh_ptr.cc (exception_ptr and dependent-exception cleanup), eh_throw.cc (primary-exception cleanup), and eh_catch.cc with the same TSan flags as the project. The support archive supplies internal terminate helpers. Standard exception layouts, atomics, and behavior are retained. The resulting symbols resolve from the executable before the uninstrumented shared library.

Scope: Linux x86_64 with libstdc++ 13/14 headers; CI pins Clang 18 and GCC 14 on Ubuntu 24.04. Revalidate this adapter when changing the compiler/runtime ABI. sys/sdt.h is supplied by systemtap-sdt-dev.

`tsan_exception_runtime` runs the independent promise/future regression, and `tsan_detector` requires an intentionally racy child to exit with a TSan diagnostic. These guard both removal of false reports and continued detection of real races.

Original license headers are preserved. COPYING3 and COPYING.RUNTIME contain GPLv3 and the GCC Runtime Library Exception 3.1. SHA256SUMS records the unchanged upstream file contents. No network download is needed during a project build.
