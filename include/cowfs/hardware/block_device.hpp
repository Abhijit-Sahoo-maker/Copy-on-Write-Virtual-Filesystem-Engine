#pragma once

#include "cowfs/common/types.hpp"
#include "cowfs/common/result.hpp"
#include <atomic>
#include <cstdint>

namespace cowfs {

struct BlockDeviceStats {
    std::atomic<uint64_t> reads_count{0};
    std::atomic<uint64_t> writes_count{0};
    std::atomic<uint64_t> bytes_read{0};
    std::atomic<uint64_t> bytes_written{0};
    std::atomic<uint64_t> syncs_count{0};

    // User payload tracker for Write Amplification Factor (WAF)
    std::atomic<uint64_t> user_payload_bytes_written{0};

    double write_amplification_factor() const noexcept {
        uint64_t user = user_payload_bytes_written.load();
        if (user == 0) return 1.0;
        return static_cast<double>(bytes_written.load()) / static_cast<double>(user);
    }
};

class IBlockDevice {
public:
    virtual ~IBlockDevice() = default;

    virtual Result<void> read_block(block_id_t block_id, uint8_t* out_buffer) = 0;
    virtual Result<void> write_block(block_id_t block_id, const uint8_t* in_buffer) = 0;
    virtual Result<void> sync() = 0;

    virtual uint64_t get_total_blocks() const noexcept = 0;
    virtual size_t get_block_size() const noexcept = 0;
    virtual bool is_open() const noexcept = 0;

    virtual const BlockDeviceStats& get_stats() const noexcept = 0;
    virtual void track_user_write(size_t bytes) noexcept = 0;
};

} // namespace cowfs
