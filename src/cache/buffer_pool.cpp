#include "cowfs/cache/buffer_pool.hpp"
#include <cstring>
#include <limits>
#include <mutex>

namespace cowfs {

BufferPool::BufferPool(IBlockDevice& device, size_t pool_size)
    : device_(device), pool_size_(pool_size) {
    frames_.reserve(pool_size_);
    for (size_t i = 0; i < pool_size_; ++i) {
        frames_.push_back(std::make_unique<Frame>());
    }
}

BufferPool::~BufferPool() {
    flush_all();
}

Result<size_t> BufferPool::find_victim_unlocked() {
    // First, look for an empty frame
    for (size_t i = 0; i < pool_size_; ++i) {
        if (frames_[i]->block_id == INVALID_BLOCK) {
            return Result<size_t>::ok(i);
        }
    }

    // Otherwise, find unpinned frame with lowest access_time (LRU)
    size_t victim_idx = pool_size_;
    uint64_t oldest_time = std::numeric_limits<uint64_t>::max();

    for (size_t i = 0; i < pool_size_; ++i) {
        if (frames_[i]->pin_count == 0) {
            if (frames_[i]->access_time < oldest_time) {
                oldest_time = frames_[i]->access_time;
                victim_idx = i;
            }
        }
    }

    if (victim_idx == pool_size_) {
        return Result<size_t>::err(FsError::BufferPoolExhausted);
    }

    // Evict victim
    auto& victim = frames_[victim_idx];
    if (victim->is_dirty && victim->block_id != INVALID_BLOCK) {
        auto write_res = device_.write_block(victim->block_id, victim->data.data());
        if (write_res.is_err()) return Result<size_t>::err(write_res.error());
        stats_.dirty_evictions++;
    }

    page_table_.erase(victim->block_id);
    victim->block_id = INVALID_BLOCK;
    victim->is_dirty = false;

    return Result<size_t>::ok(victim_idx);
}

Result<size_t> BufferPool::fetch_block(block_id_t block_id) {
    stats_.total_accesses++;
    uint64_t now = ++clock_counter_;

    std::unique_lock<std::shared_mutex> pool_lock(pool_mutex_);

    auto it = page_table_.find(block_id);
    if (it != page_table_.end()) {
        size_t frame_idx = it->second;
        frames_[frame_idx]->pin_count++;
        frames_[frame_idx]->access_time = now;
        stats_.hits++;
        return Result<size_t>::ok(frame_idx);
    }

    stats_.misses++;

    // Find victim
    auto victim_res = find_victim_unlocked();
    if (victim_res.is_err()) return victim_res;

    size_t frame_idx = victim_res.value();
    auto& frame = frames_[frame_idx];

    // Read block from device into frame
    auto read_res = device_.read_block(block_id, frame->data.data());
    if (read_res.is_err()) return Result<size_t>::err(read_res.error());

    frame->block_id = block_id;
    frame->is_dirty = false;
    frame->pin_count = 1;
    frame->access_time = now;
    page_table_[block_id] = frame_idx;

    return Result<size_t>::ok(frame_idx);
}

Result<void> BufferPool::read_block(block_id_t block_id, uint8_t* out_buffer) {
    if (!out_buffer) return Result<void>::err(FsError::InvalidArg);

    auto frame_res = fetch_block(block_id);
    if (frame_res.is_err()) return Result<void>::err(frame_res.error());

    size_t frame_idx = frame_res.value();
    {
        std::shared_lock<std::shared_mutex> frame_lock(frames_[frame_idx]->mutex);
        std::memcpy(out_buffer, frames_[frame_idx]->data.data(), BLOCK_SIZE);
    }
    unpin_frame(frame_idx, false);

    return Result<void>::ok();
}

Result<void> BufferPool::write_block(block_id_t block_id, const uint8_t* in_buffer) {
    if (!in_buffer) return Result<void>::err(FsError::InvalidArg);

    auto frame_res = fetch_block(block_id);
    if (frame_res.is_err()) return Result<void>::err(frame_res.error());

    size_t frame_idx = frame_res.value();
    {
        std::unique_lock<std::shared_mutex> frame_lock(frames_[frame_idx]->mutex);
        std::memcpy(frames_[frame_idx]->data.data(), in_buffer, BLOCK_SIZE);
    }
    unpin_frame(frame_idx, true); // mark dirty

    return Result<void>::ok();
}

void BufferPool::unpin_frame(size_t frame_idx, bool is_dirty) {
    std::unique_lock<std::shared_mutex> lock(pool_mutex_);
    if (frame_idx < frames_.size()) {
        if (frames_[frame_idx]->pin_count > 0) {
            frames_[frame_idx]->pin_count--;
        }
        if (is_dirty) {
            frames_[frame_idx]->is_dirty = true;
        }
    }
}

void BufferPool::pin_frame(size_t frame_idx) {
    std::unique_lock<std::shared_mutex> lock(pool_mutex_);
    if (frame_idx < frames_.size()) {
        frames_[frame_idx]->pin_count++;
    }
}

Result<void> BufferPool::flush_block(block_id_t block_id) {
    std::unique_lock<std::shared_mutex> lock(pool_mutex_);
    auto it = page_table_.find(block_id);
    if (it != page_table_.end()) {
        size_t idx = it->second;
        if (frames_[idx]->is_dirty) {
            auto res = device_.write_block(block_id, frames_[idx]->data.data());
            if (res.is_err()) return res;
            frames_[idx]->is_dirty = false;
        }
    }
    return Result<void>::ok();
}

Result<void> BufferPool::flush_all() {
    std::unique_lock<std::shared_mutex> lock(pool_mutex_);
    for (size_t i = 0; i < pool_size_; ++i) {
        if (frames_[i]->is_dirty && frames_[i]->block_id != INVALID_BLOCK) {
            auto res = device_.write_block(frames_[i]->block_id, frames_[i]->data.data());
            if (res.is_err()) return res;
            frames_[i]->is_dirty = false;
        }
    }
    return device_.sync();
}

void BufferPool::invalidate_block(block_id_t block_id) {
    std::unique_lock<std::shared_mutex> lock(pool_mutex_);
    auto it = page_table_.find(block_id);
    if (it != page_table_.end()) {
        size_t idx = it->second;
        frames_[idx]->block_id = INVALID_BLOCK;
        frames_[idx]->is_dirty = false;
        frames_[idx]->pin_count = 0;
        page_table_.erase(it);
    }
}

size_t BufferPool::get_dirty_count() const {
    std::shared_lock<std::shared_mutex> lock(pool_mutex_);
    size_t count = 0;
    for (const auto& f : frames_) {
        if (f->is_dirty && f->block_id != INVALID_BLOCK) count++;
    }
    return count;
}

} // namespace cowfs
