#include "test_framework.hpp"
#include "cowfs/core/filesystem.hpp"
#include "cowfs/hardware/memory_block_device.hpp"

using namespace cowfs;

bool test_cow_cloning_and_mutation() {
    constexpr uint64_t TOTAL_BLOCKS = 512;
    auto dev = std::make_unique<MemoryBlockDevice>(TOTAL_BLOCKS);
    TEST_ASSERT(FileSystem::format(*dev, TOTAL_BLOCKS).is_ok(), "format failed");

    FileSystem fs(std::move(dev));
    TEST_ASSERT(fs.mount().is_ok(), "mount failed");

    // 1. Create fileA and write data
    auto id_a_res = fs.create_file("/fileA.txt", FileType::Regular);
    TEST_ASSERT(id_a_res.is_ok(), "create fileA failed");
    inode_id_t id_a = id_a_res.value();

    std::string payload_a = "Original data in file A";
    auto w_res = fs.write_file(id_a, 0, reinterpret_cast<const uint8_t*>(payload_a.data()), payload_a.size());
    TEST_ASSERT(w_res.is_ok() && w_res.value() == payload_a.size(), "write fileA failed");

    // Inspect physical block of fileA
    auto stat_a_before = fs.stat_file(id_a);
    TEST_ASSERT(stat_a_before.is_ok() && !stat_a_before.value().allocated_blocks.empty(), "stat fileA failed");
    block_id_t block_a = stat_a_before.value().allocated_blocks[0];

    // Refcount should be 1
    TEST_ASSERT(fs.get_block_refcount(block_a).value_or(0) == 1, "Initial refcount must be 1");

    // 2. Clone fileA -> fileB (Instantaneous CoW clone!)
    auto clone_res = fs.clone_file("/fileA.txt", "/fileB.txt");
    TEST_ASSERT(clone_res.is_ok(), "clone_file failed");
    inode_id_t id_b = clone_res.value();

    // Inspect physical block of fileB
    auto stat_b = fs.stat_file(id_b);
    TEST_ASSERT(stat_b.is_ok() && !stat_b.value().allocated_blocks.empty(), "stat fileB failed");
    block_id_t block_b = stat_b.value().allocated_blocks[0];

    // Both files MUST share the EXACT SAME physical block
    TEST_ASSERT(block_a == block_b, "Cloned file must share the exact physical block");

    // Reference count must now be 2
    TEST_ASSERT(fs.get_block_refcount(block_a).value_or(0) == 2, "Refcount of shared block must be 2");

    // 3. Mutate fileB (Trigger Copy-on-Write!)
    std::string payload_b = "Mutated data in file B (COW triggered)";
    auto wb_res = fs.write_file(id_b, 0, reinterpret_cast<const uint8_t*>(payload_b.data()), payload_b.size());
    TEST_ASSERT(wb_res.is_ok(), "write fileB failed");

    // Inspect blocks after write
    auto stat_a_after = fs.stat_file(id_a);
    auto stat_b_after = fs.stat_file(id_b);

    block_id_t block_a_after = stat_a_after.value().allocated_blocks[0];
    block_id_t block_b_after = stat_b_after.value().allocated_blocks[0];

    // fileB must now have a DIFFERENT physical block
    TEST_ASSERT(block_a_after != block_b_after, "CoW must allocate a distinct block for mutated fileB");
    TEST_ASSERT(block_a_after == block_a, "fileA must retain original physical block");

    // Reference counts must both be 1
    TEST_ASSERT(fs.get_block_refcount(block_a_after).value_or(0) == 1, "fileA block refcount must decrease to 1");
    TEST_ASSERT(fs.get_block_refcount(block_b_after).value_or(0) == 1, "fileB new block refcount must be 1");

    // 4. Verify file contents independently
    char buf_a[64] = {0};
    char buf_b[64] = {0};
    fs.read_file(id_a, 0, reinterpret_cast<uint8_t*>(buf_a), sizeof(buf_a));
    fs.read_file(id_b, 0, reinterpret_cast<uint8_t*>(buf_b), sizeof(buf_b));

    TEST_ASSERT(std::string(buf_a) == payload_a, "fileA contents must remain unchanged");
    TEST_ASSERT(std::string(buf_b) == payload_b, "fileB contents must reflect mutation");

    return true;
}

int main() {
    std::cout << "--- Running Copy-on-Write Core Tests ---\n";
    RUN_TEST(test_cow_cloning_and_mutation);
    cowfs::test::print_summary();
    return (cowfs::test::failed_tests == 0) ? 0 : 1;
}
