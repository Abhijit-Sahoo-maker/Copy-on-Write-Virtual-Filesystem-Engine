#pragma once

#include "cowfs/common/types.hpp"
#include "cowfs/common/result.hpp"
#include "cowfs/common/align.hpp"
#include "cowfs/hardware/block_device.hpp"
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <atomic>
#include <memory>

namespace cowfs {

struct CacheStats {
    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> misses{0};
    std::atomic<uint64_t> dirty_evictions{0};
    std::atomic<uint64_t> total_accesses{0};

    double hit_ratio() const noexcept {
        uint64_t h = hits.load();
        uint64_t m = misses.load();
        uint64_t total = h + m;
        return (total == 0) ? 0.0 : (static_cast<double>(h) / static_cast<double>(total) * 100.0);
    }
};

// Cache Frame aligned to hardware cache line
struct alignas(CACHE_LINE_SIZE) Frame {
    block_id_t block_id{INVALID_BLOCK};
    bool is_dirty{false};
    int pin_count{0};
    uint64_t access_time{0};
    AlignedBuffer data{BLOCK_SIZE};
    mutable std::shared_mutex mutex;

    Frame() = default;
};

class BufferPool {
public:
    BufferPool(IBlockDevice& device, size_t pool_size = 256);
    ~BufferPool();

    // Fetch a block into cache, returning frame index (pinned)
    Result<size_t> fetch_block(block_id_t block_id);

    // Read data from a cached block
    Result<void> read_block(block_id_t block_id, uint8_t* out_buffer);

    // Write data to a cached block and mark dirty
    Result<void> write_block(block_id_t block_id, const uint8_t* in_buffer);

    // Unpin a frame after use
    void unpin_frame(size_t frame_idx, bool is_dirty = false);

    // Pin a frame
    void pin_frame(size_t frame_idx);

    // Flush a specific block to disk
    Result<void> flush_block(block_id_t block_id);

    // Flush all dirty blocks to disk
    Result<void> flush_all();

    // Invalidate a block in the cache (e.g. after freeing)
    void invalidate_block(block_id_t block_id);

    const CacheStats& get_stats() const noexcept { return stats_; }
    size_t get_capacity() const noexcept { return pool_size_; }
    size_t get_dirty_count() const;

private:
    IBlockDevice& device_;
    size_t pool_size_;
    std::vector<std::unique_ptr<Frame>> frames_;
    std::unordered_map<block_id_t, size_t> page_table_; // block_id -> frame_idx
    mutable std::shared_mutex pool_mutex_;
    std::atomic<uint64_t> clock_counter_{0};
    CacheStats stats_;

    // Internal helper: find victim frame for eviction (must be called with pool_mutex_ held)
    Result<size_t> find_victim_unlocked();
};

} // namespace cowfs
