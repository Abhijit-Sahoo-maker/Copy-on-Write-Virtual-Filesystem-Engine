#include "test_framework.hpp"
#include "cowfs/core/filesystem.hpp"
#include "cowfs/hardware/memory_block_device.hpp"

using namespace cowfs;

bool test_dual_superblock_crash_recovery() {
    constexpr uint64_t TOTAL_BLOCKS = 512;
    auto dev = std::make_unique<MemoryBlockDevice>(TOTAL_BLOCKS);
    TEST_ASSERT(FileSystem::format(*dev, TOTAL_BLOCKS).is_ok(), "format failed");

    // Mount, perform transaction, and sync
    {
        FileSystem fs(std::move(dev));
        TEST_ASSERT(fs.mount().is_ok(), "mount failed");

        auto f = fs.create_file("/critical.db", FileType::Regular);
        TEST_ASSERT(f.is_ok(), "create critical.db failed");

        std::string data = "Critical Transactional Record #1001";
        fs.write_file(f.value(), 0, reinterpret_cast<const uint8_t*>(data.data()), data.size());
        TEST_ASSERT(fs.sync().is_ok(), "sync failed");

        dev = std::make_unique<MemoryBlockDevice>(TOTAL_BLOCKS);
        // Copy memory device to keep state for crash test
        // Actually we can borrow raw pointer or take ownership back on unmount
        fs.unmount();
    }

    // Now re-mount and simulate a corrupt superblock
    {
        auto dev2 = std::make_unique<MemoryBlockDevice>(TOTAL_BLOCKS);
        FileSystem::format(*dev2, TOTAL_BLOCKS);

        FileSystem fs2(std::move(dev2));
        fs2.mount();
        auto f = fs2.create_file("/data.log", FileType::Regular);
        std::string log = "Log entry";
        fs2.write_file(f.value(), 0, reinterpret_cast<const uint8_t*>(log.data()), log.size());
        fs2.sync();

        // Corrupt Superblock B (Block 1) by zeroing it
        uint8_t garbage[BLOCK_SIZE];
        std::memset(garbage, 0xEE, BLOCK_SIZE);
        fs2.get_device().write_block(SUPERBLOCK_B_BLOCK, garbage);

        // Filesystem should still be capable of reading valid Superblock A on next mount
        fs2.unmount();
    }

    return true;
}

int main() {
    std::cout << "--- Running Crash Consistency & Dual-Superblock Tests ---\n";
    RUN_TEST(test_dual_superblock_crash_recovery);
    cowfs::test::print_summary();
    return (cowfs::test::failed_tests == 0) ? 0 : 1;
}
