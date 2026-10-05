#include "cowfs/storage/bitmap_allocator.hpp"
#include "cowfs/common/align.hpp"
#include <cstring>

namespace cowfs {

BitmapAllocator::BitmapAllocator(block_id_t start_block, uint32_t num_blocks, uint64_t total_system_blocks)
    : start_block_(start_block),
      bitmap_blocks_count_(num_blocks),
      total_system_blocks_(total_system_blocks),
      dirty_blocks_(num_blocks, false) {
    size_t total_bits = static_cast<size_t>(bitmap_blocks_count_) * BLOCK_SIZE * 8;
    bitmap_words_.resize(total_bits / 64, 0);
}

void BitmapAllocator::mark_bit(block_id_t block_id, bool allocated) {
    size_t word_idx = block_id / 64;
    size_t bit_idx = block_id % 64;
    uint64_t mask = 1ULL << bit_idx;

    if (allocated) {
        bitmap_words_[word_idx] |= mask;
    } else {
        bitmap_words_[word_idx] &= ~mask;
    }

    size_t disk_block_idx = (block_id / 8) / BLOCK_SIZE;
    if (disk_block_idx < dirty_blocks_.size()) {
        dirty_blocks_[disk_block_idx] = true;
    }
}

bool BitmapAllocator::is_allocated(block_id_t block_id) const {
    if (block_id >= total_system_blocks_) return true;
    std::lock_guard<std::mutex> lock(mutex_);
    size_t word_idx = block_id / 64;
    size_t bit_idx = block_id % 64;
    return (bitmap_words_[word_idx] & (1ULL << bit_idx)) != 0;
}

Result<void> BitmapAllocator::format_new(IBlockDevice& device, block_id_t first_free_data_block) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::fill(bitmap_words_.begin(), bitmap_words_.end(), 0);

    // Mark metadata blocks (from 0 to first_free_data_block - 1) as allocated
    for (block_id_t b = 0; b < first_free_data_block; ++b) {
        size_t word_idx = b / 64;
        size_t bit_idx = b % 64;
        bitmap_words_[word_idx] |= (1ULL << bit_idx);
    }

    // Any block beyond total_system_blocks_ is marked allocated
    for (uint64_t b = total_system_blocks_; b < bitmap_words_.size() * 64; ++b) {
        size_t word_idx = b / 64;
        size_t bit_idx = b % 64;
        bitmap_words_[word_idx] |= (1ULL << bit_idx);
    }

    free_blocks_count_ = (total_system_blocks_ > first_free_data_block) 
                       ? (total_system_blocks_ - first_free_data_block) : 0;

    std::fill(dirty_blocks_.begin(), dirty_blocks_.end(), true);
    return flush(device);
}

Result<void> BitmapAllocator::load(IBlockDevice& device) {
    std::lock_guard<std::mutex> lock(mutex_);
    AlignedBuffer buf(BLOCK_SIZE);

    free_blocks_count_ = 0;
    for (uint32_t i = 0; i < bitmap_blocks_count_; ++i) {
        auto res = device.read_block(start_block_ + i, buf.data());
        if (res.is_err()) return res;

        size_t words_per_block = BLOCK_SIZE / sizeof(uint64_t);
        const auto* src_words = reinterpret_cast<const uint64_t*>(buf.data());
        for (size_t w = 0; w < words_per_block; ++w) {
            size_t global_word = i * words_per_block + w;
            if (global_word < bitmap_words_.size()) {
                bitmap_words_[global_word] = src_words[w];
            }
        }
        dirty_blocks_[i] = false;
    }

    // Count free blocks
    for (block_id_t b = 0; b < total_system_blocks_; ++b) {
        size_t w = b / 64;
        size_t bit = b % 64;
        if ((bitmap_words_[w] & (1ULL << bit)) == 0) {
            free_blocks_count_++;
        }
    }

    return Result<void>::ok();
}

Result<void> BitmapAllocator::flush(IBlockDevice& device) {
    AlignedBuffer buf(BLOCK_SIZE);
    size_t words_per_block = BLOCK_SIZE / sizeof(uint64_t);

    for (uint32_t i = 0; i < bitmap_blocks_count_; ++i) {
        if (!dirty_blocks_[i]) continue;

        auto* dest_words = reinterpret_cast<uint64_t*>(buf.data());
        for (size_t w = 0; w < words_per_block; ++w) {
            size_t global_word = i * words_per_block + w;
            dest_words[w] = (global_word < bitmap_words_.size()) ? bitmap_words_[global_word] : ~0ULL;
        }

        auto res = device.write_block(start_block_ + i, buf.data());
        if (res.is_err()) return res;

        dirty_blocks_[i] = false;
    }

    return Result<void>::ok();
}

Result<block_id_t> BitmapAllocator::allocate_block() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (free_blocks_count_ == 0) {
        return Result<block_id_t>::err(FsError::NoSpaceLeft);
    }

    // Fast word scanning with std::countr_zero CPU intrinsic
    for (size_t w = 0; w < bitmap_words_.size(); ++w) {
        uint64_t word = bitmap_words_[w];
        if (word != ~0ULL) { // Found word with at least one free 0-bit
            int free_bit = std::countr_zero(~word);
            block_id_t candidate = static_cast<block_id_t>(w * 64 + free_bit);
            if (candidate < total_system_blocks_) {
                mark_bit(candidate, true);
                free_blocks_count_--;
                return Result<block_id_t>::ok(candidate);
            }
        }
    }

    return Result<block_id_t>::err(FsError::NoSpaceLeft);
}

Result<std::vector<block_id_t>> BitmapAllocator::allocate_contiguous(size_t count) {
    if (count == 0) return Result<std::vector<block_id_t>>::ok({});
    std::lock_guard<std::mutex> lock(mutex_);

    if (free_blocks_count_ < count) {
        return Result<std::vector<block_id_t>>::err(FsError::NoSpaceLeft);
    }

    // Try finding contiguous run
    size_t current_run = 0;
    block_id_t run_start = 0;

    for (block_id_t b = 0; b < total_system_blocks_; ++b) {
        size_t w = b / 64;
        size_t bit = b % 64;
        bool allocated = (bitmap_words_[w] & (1ULL << bit)) != 0;

        if (!allocated) {
            if (current_run == 0) run_start = b;
            current_run++;
            if (current_run == count) {
                // Found contiguous extent!
                std::vector<block_id_t> allocated_blocks;
                allocated_blocks.reserve(count);
                for (size_t i = 0; i < count; ++i) {
                    block_id_t blk = run_start + static_cast<block_id_t>(i);
                    mark_bit(blk, true);
                    allocated_blocks.push_back(blk);
                }
                free_blocks_count_ -= count;
                return Result<std::vector<block_id_t>>::ok(allocated_blocks);
            }
        } else {
            current_run = 0;
        }
    }

    // Fallback: non-contiguous allocation
    std::vector<block_id_t> allocated_blocks;
    allocated_blocks.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        // Unlock not needed as we can use private helper loop
        bool found = false;
        for (size_t w = 0; w < bitmap_words_.size(); ++w) {
            uint64_t word = bitmap_words_[w];
            if (word != ~0ULL) {
                int free_bit = std::countr_zero(~word);
                block_id_t candidate = static_cast<block_id_t>(w * 64 + free_bit);
                if (candidate < total_system_blocks_) {
                    mark_bit(candidate, true);
                    allocated_blocks.push_back(candidate);
                    free_blocks_count_--;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            // Rollback
            for (auto blk : allocated_blocks) {
                mark_bit(blk, false);
                free_blocks_count_++;
            }
            return Result<std::vector<block_id_t>>::err(FsError::NoSpaceLeft);
        }
    }

    return Result<std::vector<block_id_t>>::ok(allocated_blocks);
}

Result<void> BitmapAllocator::free_block(block_id_t block_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (block_id >= total_system_blocks_) {
        return Result<void>::err(FsError::InvalidArg);
    }

    size_t w = block_id / 64;
    size_t bit = block_id % 64;
    if ((bitmap_words_[w] & (1ULL << bit)) != 0) {
        mark_bit(block_id, false);
        free_blocks_count_++;
    }

    return Result<void>::ok();
}

} // namespace cowfs
