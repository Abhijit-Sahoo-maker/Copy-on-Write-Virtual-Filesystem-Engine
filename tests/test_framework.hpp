#pragma once

#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <functional>

namespace cowfs::test {

inline int total_tests = 0;
inline int passed_tests = 0;
inline int failed_tests = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "\033[31m  [FAILED]\033[0m " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

#define RUN_TEST(func) \
    do { \
        cowfs::test::total_tests++; \
        std::cout << "\033[36m[RUNNING]\033[0m " << #func << " ... " << std::flush; \
        auto start = std::chrono::high_resolution_clock::now(); \
        bool ok = func(); \
        auto end = std::chrono::high_resolution_clock::now(); \
        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count(); \
        if (ok) { \
            cowfs::test::passed_tests++; \
            std::cout << "\033[32m[PASSED]\033[0m (" << duration_us << " us)\n"; \
        } else { \
            cowfs::test::failed_tests++; \
        } \
    } while (0)

inline void print_summary() {
    std::cout << "\n==========================================\n";
    std::cout << "TEST SUMMARY: "
              << "\033[32m" << passed_tests << " Passed\033[0m, "
              << (failed_tests > 0 ? "\033[31m" : "\033[0m") << failed_tests << " Failed\033[0m, "
              << total_tests << " Total\n";
    std::cout << "==========================================\n";
}

} // namespace cowfs::test
