cmake_minimum_required (VERSION 3.10)

option(BENCHMARK_ENABLE_TESTING "" OFF)
option(BENCHMARK_ENABLE_LTO "" ON)
add_subdirectory(benchmark)

add_executable(thread_wakeup
    thread_wakeup.cc
)
target_link_libraries(thread_wakeup benchmark)
