cmake_minimum_required(VERSION 3.25)
project(OrderBook VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warning flags shared by every target we build.
# /Zc:__cplusplus makes MSVC report the real standard version in __cplusplus.
function(set_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Core order book library
add_library(orderbook STATIC
    src/map_order_book.cpp
    src/lobster.cpp
    src/replayer.cpp
)
target_include_directories(orderbook PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_project_warnings(orderbook)

# Demo executable
add_executable(orderbook_app src/main.cpp)
target_link_libraries(orderbook_app PRIVATE orderbook)
set_project_warnings(orderbook_app)

# LOBSTER replay tool: replay <message.csv> <orderbook.csv> [--csv out.csv]
add_executable(replay src/replay_main.cpp)
target_link_libraries(replay PRIVATE orderbook)
set_project_warnings(replay)

# Benchmarks (Google Benchmark). OB_DATA_DIR lets the replay benchmark find
# the LOBSTER files without depending on the working directory.
find_package(benchmark CONFIG REQUIRED)
add_executable(bench bench/book_bench.cpp)
target_link_libraries(bench PRIVATE orderbook benchmark::benchmark benchmark::benchmark_main)
target_compile_definitions(bench PRIVATE OB_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
set_project_warnings(bench)

# Per-event latency percentiles: latency <message.csv> <orderbook.csv> [--csv out.csv]
add_executable(latency bench/latency_main.cpp)
target_link_libraries(latency PRIVATE orderbook)
set_project_warnings(latency)

# Tests
enable_testing()
find_package(GTest CONFIG REQUIRED)

add_executable(tests
    tests/sanity_test.cpp
    tests/types_test.cpp
    tests/order_book_tests.cpp
    tests/matching_tests.cpp
    tests/cancel_modify_tests.cpp
    tests/lobster_tests.cpp
    tests/replayer_tests.cpp
)
target_link_libraries(tests PRIVATE orderbook GTest::gtest_main)
set_project_warnings(tests)

include(GoogleTest)
gtest_discover_tests(tests DISCOVERY_MODE PRE_TEST)
