#pragma once

#include "cowfs/common/types.hpp"
#include "cowfs/common/result.hpp"
#include "cowfs/hardware/block_device.hpp"
#include <vector>
#include <mutex>

namespace cowfs {

class RefCountTable {
public:
    RefCountTable(block_id_t start_block, uint32_t num_blocks, uint64_t total_system_blocks);

    Result<void> load(IBlockDevice& device);
    Result<void> flush(IBlockDevice& device);
    Result<void> format_new(IBlockDevice& device, block_id_t first_free_data_block);

    Result<uint16_t> get_refcount(block_id_t block_id) const;
    Result<uint16_t> increment(block_id_t block_id);
    Result<uint16_t> decrement(block_id_t block_id);
    Result<void> set_refcount(block_id_t block_id, uint16_t count);

    // Checks if the block is shared (refcount > 1, requiring Copy-on-Write)
    bool is_shared(block_id_t block_id) const;

private:
    block_id_t start_block_;
    uint32_t num_blocks_;
    uint64_t total_system_blocks_;

    std::vector<uint16_t> refcounts_;
    std::vector<bool> dirty_blocks_;
    mutable std::mutex mutex_;
};

} // namespace cowfs
