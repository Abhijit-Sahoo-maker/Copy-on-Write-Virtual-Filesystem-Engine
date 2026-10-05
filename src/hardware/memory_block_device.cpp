#include "cowfs/hardware/memory_block_device.hpp"
#include <cstring>
#include <mutex>

namespace cowfs {

MemoryBlockDevice::MemoryBlockDevice(uint64_t total_blocks, size_t block_size)
    : total_blocks_(total_blocks), block_size_(block_size), storage_(total_blocks * block_size, 0) {}

Result<void> MemoryBlockDevice::read_block(block_id_t block_id, uint8_t* out_buffer) {
    if (!out_buffer) return Result<void>::err(FsError::InvalidArg);
    if (block_id >= total_blocks_) return Result<void>::err(FsError::InvalidArg);

    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::memcpy(out_buffer, storage_.data() + (block_id * block_size_), block_size_);
    }

    stats_.reads_count++;
    stats_.bytes_read += block_size_;
    return Result<void>::ok();
}

Result<void> MemoryBlockDevice::write_block(block_id_t block_id, const uint8_t* in_buffer) {
    if (!in_buffer) return Result<void>::err(FsError::InvalidArg);
    if (block_id >= total_blocks_) return Result<void>::err(FsError::InvalidArg);

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        std::memcpy(storage_.data() + (block_id * block_size_), in_buffer, block_size_);
    }

    stats_.writes_count++;
    stats_.bytes_written += block_size_;
    return Result<void>::ok();
}

Result<void> MemoryBlockDevice::sync() {
    stats_.syncs_count++;
    return Result<void>::ok();
}

} // namespace cowfs
