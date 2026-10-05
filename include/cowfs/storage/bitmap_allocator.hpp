#pragma once

#include "cowfs/common/types.hpp"
#include "cowfs/common/result.hpp"
#include "cowfs/hardware/block_device.hpp"
#include <vector>
#include <bit>
#include <mutex>
#include <optional>

namespace cowfs {

class BitmapAllocator {
public:
    BitmapAllocator(block_id_t start_block, uint32_t num_blocks, uint64_t total_system_blocks);

    // Initialize in-memory bitmap and sync from device
    Result<void> load(IBlockDevice& device);

    // Flush dirty bitmap blocks to device
    Result<void> flush(IBlockDevice& device);

    // Format new bitmap marking metadata blocks as used and data blocks as free
    Result<void> format_new(IBlockDevice& device, block_id_t first_free_data_block);

    // Allocate a single free block
    Result<block_id_t> allocate_block();

    // Allocate N contiguous blocks (Extent allocation)
    Result<std::vector<block_id_t>> allocate_contiguous(size_t count);

    // Free an allocated block
    Result<void> free_block(block_id_t block_id);

    // Check if block is allocated
    bool is_allocated(block_id_t block_id) const;

    uint64_t get_free_blocks_count() const noexcept { return free_blocks_count_; }
    uint64_t get_total_managed_blocks() const noexcept { return total_system_blocks_; }

private:
    block_id_t start_block_;
    uint32_t bitmap_blocks_count_;
    uint64_t total_system_blocks_;
    uint64_t free_blocks_count_{0};

    // Stored as 64-bit words for fast CPU bitwise intrinsics (std::countr_zero)
    std::vector<uint64_t> bitmap_words_;
    std::vector<bool> dirty_blocks_;
    mutable std::mutex mutex_;

    void mark_bit(block_id_t block_id, bool allocated);
};

} // namespace cowfs
