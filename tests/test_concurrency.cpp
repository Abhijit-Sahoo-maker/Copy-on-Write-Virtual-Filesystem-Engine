#include "test_framework.hpp"
#include "cowfs/core/filesystem.hpp"
#include "cowfs/hardware/memory_block_device.hpp"
#include <thread>
#include <vector>
#include <atomic>

using namespace cowfs;

bool test_multithreaded_read_write() {
    constexpr uint64_t TOTAL_BLOCKS = 1024;
    auto dev = std::make_unique<MemoryBlockDevice>(TOTAL_BLOCKS);
    TEST_ASSERT(FileSystem::format(*dev, TOTAL_BLOCKS).is_ok(), "format failed");

    FileSystem fs(std::move(dev));
    TEST_ASSERT(fs.mount().is_ok(), "mount failed");

    // Create a base file
    auto f_res = fs.create_file("/concurrent.dat", FileType::Regular);
    TEST_ASSERT(f_res.is_ok(), "create concurrent.dat failed");
    inode_id_t file_id = f_res.value();

    std::vector<uint8_t> initial_data(4096, 'A');
    fs.write_file(file_id, 0, initial_data.data(), initial_data.size());

    std::atomic<bool> start_flag{false};
    std::atomic<int> read_errors{0};
    std::atomic<int> write_errors{0};

    constexpr int NUM_READERS = 4;
    constexpr int NUM_WRITERS = 2;
    constexpr int ITERATIONS = 100;

    std::vector<std::thread> threads;

    // Launch readers
    for (int r = 0; r < NUM_READERS; ++r) {
        threads.emplace_back([&, r]() {
            while (!start_flag.load()) std::this_thread::yield();

            std::vector<uint8_t> read_buf(1024);
            for (int i = 0; i < ITERATIONS; ++i) {
                auto res = fs.read_file(file_id, 0, read_buf.data(), read_buf.size());
                if (res.is_err() || res.value() == 0) {
                    read_errors++;
                }
            }
        });
    }

    // Launch writers
    for (int w = 0; w < NUM_WRITERS; ++w) {
        threads.emplace_back([&, w]() {
            while (!start_flag.load()) std::this_thread::yield();

            std::vector<uint8_t> write_buf(512, static_cast<uint8_t>('B' + w));
            for (int i = 0; i < ITERATIONS; ++i) {
                auto res = fs.write_file(file_id, 0, write_buf.data(), write_buf.size());
                if (res.is_err()) {
                    write_errors++;
                }
            }
        });
    }

    start_flag.store(true);
    for (auto& t : threads) {
        t.join();
    }

    TEST_ASSERT(read_errors.load() == 0, "Concurrent readers reported errors");
    TEST_ASSERT(write_errors.load() == 0, "Concurrent writers reported errors");

    return true;
}

int main() {
    std::cout << "--- Running Multithreaded Concurrency Tests ---\n";
    RUN_TEST(test_multithreaded_read_write);
    cowfs::test::print_summary();
    return (cowfs::test::failed_tests == 0) ? 0 : 1;
}
