#include "cowfs/vfs/vfs.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>
#include <numeric>

using namespace cowfs;

void print_metric(const std::string& name, double value, const std::string& unit) {
    std::cout << "  " << std::left << std::setw(35) << name << ": "
              << std::right << std::setw(10) << std::fixed << std::setprecision(2) << value
              << " " << unit << "\n";
}

void benchmark_sequential_write(VFS& vfs, size_t total_mb) {
    size_t total_bytes = total_mb * 1024 * 1024;
    std::vector<uint8_t> chunk(64 * 1024, 0xAA); // 64 KB write buffer

    int fd = vfs.open("/bench_seq.bin", OpenFlags::Create | OpenFlags::WriteOnly | OpenFlags::Truncate);

    auto start = std::chrono::high_resolution_clock::now();
    size_t written = 0;
    while (written < total_bytes) {
        written += vfs.write(fd, chunk.data(), chunk.size());
    }
    vfs.sync();
    auto end = std::chrono::high_resolution_clock::now();

    vfs.close(fd);

    double sec = std::chrono::duration<double>(end - start).count();
    double mb_per_sec = static_cast<double>(total_mb) / sec;

    print_metric("Sequential Write Throughput", mb_per_sec, "MB/s");
}

void benchmark_snapshot_latency(VFS& vfs) {
    // Measure time taken to create a snapshot
    auto start = std::chrono::high_resolution_clock::now();
    vfs.snapshot_create("perf_snap");
    auto end = std::chrono::high_resolution_clock::now();

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    print_metric("Snapshot Creation Latency (O(1))", static_cast<double>(us), "us");
}

void benchmark_cow_clone_vs_deep_copy(VFS& vfs) {
    // 1. Measure CoW zero-copy clone
    auto start_cow = std::chrono::high_resolution_clock::now();
    vfs.clone("/bench_seq.bin", "/bench_seq_cow.bin");
    auto end_cow = std::chrono::high_resolution_clock::now();
    auto us_cow = std::chrono::duration_cast<std::chrono::microseconds>(end_cow - start_cow).count();

    // 2. Measure standard read-and-write physical copy
    auto start_phys = std::chrono::high_resolution_clock::now();
    int src_fd = vfs.open("/bench_seq.bin", OpenFlags::ReadOnly);
    int dst_fd = vfs.open("/bench_seq_phys.bin", OpenFlags::Create | OpenFlags::WriteOnly | OpenFlags::Truncate);
    std::vector<uint8_t> buf(64 * 1024);
    ssize_t n = 0;
    while ((n = vfs.read(src_fd, buf.data(), buf.size())) > 0) {
        vfs.write(dst_fd, buf.data(), n);
    }
    vfs.close(src_fd);
    vfs.close(dst_fd);
    auto end_phys = std::chrono::high_resolution_clock::now();
    auto us_phys = std::chrono::duration_cast<std::chrono::microseconds>(end_phys - start_phys).count();

    print_metric("CoW Zero-Copy Clone Time", static_cast<double>(us_cow), "us");
    print_metric("Physical Block Deep Copy Time", static_cast<double>(us_phys), "us");
    double speedup = (us_cow > 0) ? (static_cast<double>(us_phys) / static_cast<double>(us_cow)) : 1.0;
    print_metric("CoW Cloning Speedup Factor", speedup, "x faster");
}

void benchmark_page_cache(VFS& vfs) {
    int fd = vfs.open("/bench_seq.bin", OpenFlags::ReadOnly);
    std::vector<uint8_t> buf(4096);

    // Warm up cache
    vfs.read(fd, buf.data(), buf.size());

    // Measure cached read
    auto start = std::chrono::high_resolution_clock::now();
    constexpr int ITERS = 10000;
    for (int i = 0; i < ITERS; ++i) {
        vfs.lseek(fd, 0, SeekOrigin::Set);
        vfs.read(fd, buf.data(), buf.size());
    }
    auto end = std::chrono::high_resolution_clock::now();
    vfs.close(fd);

    auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double ns_per_read = static_cast<double>(total_ns) / ITERS;
    print_metric("Page Cache Hit Read Latency", ns_per_read, "ns / read");

    auto cs = vfs.cache_stats();
    if (cs) {
        print_metric("Page Cache Hit Ratio", cs->hit_ratio(), "%");
    }
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << "     COWFS PERFORMANCE & ARCHITECTURE BENCHMARK        \n";
    std::cout << "=======================================================\n\n";

    VFS vfs;
    constexpr size_t DISK_MB = 32;
    uint64_t total_blocks = (DISK_MB * 1024 * 1024) / BLOCK_SIZE;

    vfs.mkfs(":memory:", total_blocks);
    vfs.mount(":memory:");

    std::cout << "[1] Sequential Throughput Benchmark (4 MB payload):\n";
    benchmark_sequential_write(vfs, 4);

    std::cout << "\n[2] Snapshot Latency Benchmark:\n";
    benchmark_snapshot_latency(vfs);

    std::cout << "\n[3] Copy-on-Write Clone vs Deep Copy Benchmark:\n";
    benchmark_cow_clone_vs_deep_copy(vfs);

    std::cout << "\n[4] Page Cache Performance Benchmark:\n";
    benchmark_page_cache(vfs);

    auto io = vfs.io_stats();
    if (io) {
        std::cout << "\n[5] Hardware Storage & Architecture Metrics:\n";
        print_metric("Write Amplification Factor (WAF)", io->write_amplification_factor(), "x");
    }

    std::cout << "\n=======================================================\n";
    std::cout << "Benchmark run complete.\n";
    return 0;
}
