#include "test_framework.hpp"
#include "cowfs/storage/bitmap_allocator.hpp"
#include "cowfs/hardware/memory_block_device.hpp"

using namespace cowfs;

bool test_bitmap_allocation() {
    constexpr uint64_t TOTAL_BLOCKS = 1024;
    MemoryBlockDevice dev(TOTAL_BLOCKS);

    BitmapAllocator allocator(2, 1, TOTAL_BLOCKS);
    TEST_ASSERT(allocator.format_new(dev, 32).is_ok(), "format_new failed");
    TEST_ASSERT(allocator.get_free_blocks_count() == (TOTAL_BLOCKS - 32), "Initial free blocks mismatch");

    // Allocate first block
    auto b1 = allocator.allocate_block();
    TEST_ASSERT(b1.is_ok(), "allocate_block failed");
    TEST_ASSERT(b1.value() == 32, "First allocated data block should be 32");
    TEST_ASSERT(allocator.is_allocated(32), "Block 32 must be allocated");

    // Free and reallocate
    TEST_ASSERT(allocator.free_block(32).is_ok(), "free_block failed");
    TEST_ASSERT(!allocator.is_allocated(32), "Block 32 must be free");

    auto b2 = allocator.allocate_block();
    TEST_ASSERT(b2.is_ok() && b2.value() == 32, "Block 32 should be recycled first");

    return true;
}

bool test_contiguous_extents() {
    constexpr uint64_t TOTAL_BLOCKS = 256;
    MemoryBlockDevice dev(TOTAL_BLOCKS);

    BitmapAllocator allocator(2, 1, TOTAL_BLOCKS);
    allocator.format_new(dev, 10);

    auto ext_res = allocator.allocate_contiguous(8);
    TEST_ASSERT(ext_res.is_ok(), "allocate_contiguous failed");
    auto ext = ext_res.value();
    TEST_ASSERT(ext.size() == 8, "Expected 8 contiguous blocks");

    for (size_t i = 0; i < 8; ++i) {
        TEST_ASSERT(ext[i] == 10 + i, "Blocks must be contiguous");
    }

    return true;
}

int main() {
    std::cout << "--- Running Bitmap Allocator Tests ---\n";
    RUN_TEST(test_bitmap_allocation);
    RUN_TEST(test_contiguous_extents);
    cowfs::test::print_summary();
    return (cowfs::test::failed_tests == 0) ? 0 : 1;
}
