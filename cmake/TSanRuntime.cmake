# Compile the upstream exception reference-count operations with TSan. The system
# libstdc++ was built without instrumentation, hiding their release/acquire edges.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <bits/c++config.h>
#if !defined(_GLIBCXX_RELEASE) || _GLIBCXX_RELEASE < 13 || _GLIBCXX_RELEASE > 14
#error This runtime adapter is validated with libstdc++ 13/14 headers
#endif
int main() { return 0; }
" KVRPC_TSAN_SUPPORTED_LIBSTDCXX)
if(NOT KVRPC_TSAN_SUPPORTED_LIBSTDCXX OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    message(FATAL_ERROR "KVRPC TSan runtime currently requires Linux x86_64 and libstdc++ 13/14 headers")
endif()
check_include_file_cxx("sys/sdt.h" KVRPC_HAVE_SDT_HEADER)
if(NOT KVRPC_HAVE_SDT_HEADER)
    message(FATAL_ERROR "KVRPC TSan runtime requires systemtap-sdt-dev")
endif()
set(KVRPC_CXXABI_SOURCE "${PROJECT_SOURCE_DIR}/third_party/gcc-14.2.0/libsupc++")
target_sources(kvrpc PRIVATE
    "${KVRPC_CXXABI_SOURCE}/eh_ptr.cc"
    "${KVRPC_CXXABI_SOURCE}/eh_catch.cc"
    "${KVRPC_CXXABI_SOURCE}/eh_throw.cc")
# Supplies internal __terminate helpers. Reference-counted paths above are
# resolved from our instrumented objects before the system support archive.
target_link_libraries(kvrpc PUBLIC supc++)
