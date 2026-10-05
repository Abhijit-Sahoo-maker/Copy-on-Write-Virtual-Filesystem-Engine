cmake_minimum_required(VERSION 3.20)
project(CowFS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization and Warning flags
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -O3 -pthread)
elseif (MSVC)
    add_compile_options(/W4 /O2 /permissive-)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Core Library Sources
set(COWFS_LIB_SOURCES
    src/common/error.cpp
    src/common/crc32.cpp
    src/hardware/memory_block_device.cpp
    src/hardware/file_block_device.cpp
    src/storage/superblock.cpp
    src/storage/bitmap_allocator.cpp
    src/storage/refcount_table.cpp
    src/cache/buffer_pool.cpp
    src/core/filesystem.cpp
    src/vfs/file_descriptor.cpp
    src/vfs/vfs.cpp
)

add_library(cowfs_core STATIC ${COWFS_LIB_SOURCES})

# Interactive CLI Shell
add_executable(cowfs-shell src/shell/main.cpp)
target_link_libraries(cowfs-shell PRIVATE cowfs_core)

# Benchmarks
add_executable(benchmark_suite benchmarks/benchmark_suite.cpp)
target_link_libraries(benchmark_suite PRIVATE cowfs_core)

# Tests
enable_testing()

set(TEST_TARGETS
    test_allocator
    test_cow
    test_snapshot
    test_crash_consistency
    test_concurrency
    test_vfs
)

foreach(test_name ${TEST_TARGETS})
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE cowfs_core)
    target_include_directories(${test_name} PRIVATE tests)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
