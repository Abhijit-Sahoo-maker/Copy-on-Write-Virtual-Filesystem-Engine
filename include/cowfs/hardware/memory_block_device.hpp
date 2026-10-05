#pragma once

#include "cowfs/hardware/block_device.hpp"
#include <vector>
#include <shared_mutex>

namespace cowfs {

class MemoryBlockDevice : public IBlockDevice {
public:
    explicit MemoryBlockDevice(uint64_t total_blocks, size_t block_size = BLOCK_SIZE);
    ~MemoryBlockDevice() override = default;

    Result<void> read_block(block_id_t block_id, uint8_t* out_buffer) override;
    Result<void> write_block(block_id_t block_id, const uint8_t* in_buffer) override;
    Result<void> sync() override;

    uint64_t get_total_blocks() const noexcept override { return total_blocks_; }
    size_t get_block_size() const noexcept override { return block_size_; }
    bool is_open() const noexcept override { return true; }

    const BlockDeviceStats& get_stats() const noexcept override { return stats_; }
    void track_user_write(size_t bytes) noexcept override { stats_.user_payload_bytes_written += bytes; }

private:
    uint64_t total_blocks_;
    size_t block_size_;
    std::vector<uint8_t> storage_;
    mutable std::shared_mutex mutex_;
    BlockDeviceStats stats_;
};

} // namespace cowfs
