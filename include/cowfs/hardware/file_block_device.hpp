#pragma once

#include "cowfs/hardware/block_device.hpp"
#include <string>
#include <shared_mutex>

namespace cowfs {

class FileBlockDevice : public IBlockDevice {
public:
    static Result<std::unique_ptr<FileBlockDevice>> create_or_open(
        const std::string& filepath,
        uint64_t total_blocks,
        size_t block_size = BLOCK_SIZE
    );

    static Result<std::unique_ptr<FileBlockDevice>> open_existing(
        const std::string& filepath,
        size_t block_size = BLOCK_SIZE
    );

    ~FileBlockDevice() override;

    Result<void> read_block(block_id_t block_id, uint8_t* out_buffer) override;
    Result<void> write_block(block_id_t block_id, const uint8_t* in_buffer) override;
    Result<void> sync() override;

    uint64_t get_total_blocks() const noexcept override { return total_blocks_; }
    size_t get_block_size() const noexcept override { return block_size_; }
    bool is_open() const noexcept override { return fd_ >= 0; }

    const BlockDeviceStats& get_stats() const noexcept override { return stats_; }
    void track_user_write(size_t bytes) noexcept override { stats_.user_payload_bytes_written += bytes; }

    const std::string& get_filepath() const noexcept { return filepath_; }

private:
    FileBlockDevice(std::string filepath, int fd, uint64_t total_blocks, size_t block_size);

    std::string filepath_;
    int fd_;
    uint64_t total_blocks_;
    size_t block_size_;
    mutable std::shared_mutex mutex_;
    BlockDeviceStats stats_;
};

} // namespace cowfs
