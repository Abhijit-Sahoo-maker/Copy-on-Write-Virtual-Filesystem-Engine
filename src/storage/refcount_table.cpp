#include "cowfs/storage/refcount_table.hpp"
#include "cowfs/common/align.hpp"
#include <cstring>

namespace cowfs {

RefCountTable::RefCountTable(block_id_t start_block, uint32_t num_blocks, uint64_t total_system_blocks)
    : start_block_(start_block),
      num_blocks_(num_blocks),
      total_system_blocks_(total_system_blocks),
      refcounts_(total_system_blocks, 0),
      dirty_blocks_(num_blocks, false) {}

Result<void> RefCountTable::format_new(IBlockDevice& device, block_id_t first_free_data_block) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::fill(refcounts_.begin(), refcounts_.end(), 0);

    // Metadata blocks have refcount 1
    for (block_id_t b = 0; b < first_free_data_block && b < total_system_blocks_; ++b) {
        refcounts_[b] = 1;
    }

    std::fill(dirty_blocks_.begin(), dirty_blocks_.end(), true);
    return flush(device);
}

Result<void> RefCountTable::load(IBlockDevice& device) {
    std::lock_guard<std::mutex> lock(mutex_);
    AlignedBuffer buf(BLOCK_SIZE);
    size_t entries_per_block = BLOCK_SIZE / sizeof(uint16_t);

    for (uint32_t i = 0; i < num_blocks_; ++i) {
        auto res = device.read_block(start_block_ + i, buf.data());
        if (res.is_err()) return res;

        const auto* src = reinterpret_cast<const uint16_t*>(buf.data());
        for (size_t k = 0; k < entries_per_block; ++k) {
            size_t global_idx = i * entries_per_block + k;
            if (global_idx < refcounts_.size()) {
                refcounts_[global_idx] = src[k];
            }
        }
        dirty_blocks_[i] = false;
    }

    return Result<void>::ok();
}

Result<void> RefCountTable::flush(IBlockDevice& device) {
    AlignedBuffer buf(BLOCK_SIZE);
    size_t entries_per_block = BLOCK_SIZE / sizeof(uint16_t);

    for (uint32_t i = 0; i < num_blocks_; ++i) {
        if (!dirty_blocks_[i]) continue;

        auto* dest = reinterpret_cast<uint16_t*>(buf.data());
        for (size_t k = 0; k < entries_per_block; ++k) {
            size_t global_idx = i * entries_per_block + k;
            dest[k] = (global_idx < refcounts_.size()) ? refcounts_[global_idx] : 0;
        }

        auto res = device.write_block(start_block_ + i, buf.data());
        if (res.is_err()) return res;

        dirty_blocks_[i] = false;
    }

    return Result<void>::ok();
}

Result<uint16_t> RefCountTable::get_refcount(block_id_t block_id) const {
    if (block_id >= total_system_blocks_) {
        return Result<uint16_t>::err(FsError::InvalidArg);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return Result<uint16_t>::ok(refcounts_[block_id]);
}

Result<uint16_t> RefCountTable::increment(block_id_t block_id) {
    if (block_id >= total_system_blocks_) {
        return Result<uint16_t>::err(FsError::InvalidArg);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (refcounts_[block_id] == 0xFFFF) {
        return Result<uint16_t>::err(FsError::RefcountOverflow);
    }

    refcounts_[block_id]++;
    size_t disk_block_idx = (block_id * sizeof(uint16_t)) / BLOCK_SIZE;
    if (disk_block_idx < dirty_blocks_.size()) {
        dirty_blocks_[disk_block_idx] = true;
    }

    return Result<uint16_t>::ok(refcounts_[block_id]);
}

Result<uint16_t> RefCountTable::decrement(block_id_t block_id) {
    if (block_id >= total_system_blocks_) {
        return Result<uint16_t>::err(FsError::InvalidArg);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (refcounts_[block_id] == 0) {
        return Result<uint16_t>::err(FsError::InvalidArg);
    }

    refcounts_[block_id]--;
    size_t disk_block_idx = (block_id * sizeof(uint16_t)) / BLOCK_SIZE;
    if (disk_block_idx < dirty_blocks_.size()) {
        dirty_blocks_[disk_block_idx] = true;
    }

    return Result<uint16_t>::ok(refcounts_[block_id]);
}

Result<void> RefCountTable::set_refcount(block_id_t block_id, uint16_t count) {
    if (block_id >= total_system_blocks_) {
        return Result<void>::err(FsError::InvalidArg);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    refcounts_[block_id] = count;
    size_t disk_block_idx = (block_id * sizeof(uint16_t)) / BLOCK_SIZE;
    if (disk_block_idx < dirty_blocks_.size()) {
        dirty_blocks_[disk_block_idx] = true;
    }
    return Result<void>::ok();
}

bool RefCountTable::is_shared(block_id_t block_id) const {
    if (block_id >= total_system_blocks_) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    return refcounts_[block_id] > 1;
}

} // namespace cowfs
