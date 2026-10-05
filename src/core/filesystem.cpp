#include "cowfs/core/filesystem.hpp"
#include "cowfs/common/align.hpp"
#include <sstream>
#include <algorithm>
#include <cstring>

namespace cowfs {

FileSystem::FileSystem(std::unique_ptr<IBlockDevice> device, size_t cache_frames)
    : device_(std::move(device)) {
    buffer_pool_ = std::make_unique<BufferPool>(*device_, cache_frames);
}

FileSystem::~FileSystem() {
    if (is_mounted_) {
        unmount();
    }
}

Result<void> FileSystem::format(IBlockDevice& device, uint64_t total_blocks) {
    if (total_blocks < 64) {
        return Result<void>::err(FsError::InvalidArg);
    }

    DiskGeometry geo = DiskGeometry::compute(total_blocks);

    // 1. Initialize and write dual superblocks
    auto sb_res = SuperblockManager::create_default(geo);
    if (sb_res.is_err()) return Result<void>::err(sb_res.error());
    SuperblockHeader sb = sb_res.value();

    AlignedBuffer sb_buf(BLOCK_SIZE);
    std::memcpy(sb_buf.data(), &sb, sizeof(SuperblockHeader));

    auto w1 = device.write_block(SUPERBLOCK_A_BLOCK, sb_buf.data());
    if (w1.is_err()) return w1;
    auto w2 = device.write_block(SUPERBLOCK_B_BLOCK, sb_buf.data());
    if (w2.is_err()) return w2;

    // 2. Initialize free block bitmap
    BitmapAllocator allocator(geo.bitmap_start, geo.bitmap_blocks, total_blocks);
    auto alloc_res = allocator.format_new(device, geo.data_start);
    if (alloc_res.is_err()) return alloc_res;

    // 3. Initialize refcount table
    RefCountTable ref_table(geo.refcount_start, geo.refcount_blocks, total_blocks);
    auto ref_res = ref_table.format_new(device, geo.data_start);
    if (ref_res.is_err()) return ref_res;

    // 4. Initialize inode table blocks with zeroed inodes
    AlignedBuffer inode_buf(BLOCK_SIZE);
    inode_buf.zero();
    for (uint32_t i = 0; i < geo.inode_table_blocks; ++i) {
        auto w_inode = device.write_block(geo.inode_table_start + i, inode_buf.data());
        if (w_inode.is_err()) return w_inode;
    }

    // 5. Create root directory inode (Inode 1)
    DiskInode root_inode;
    root_inode.reset();
    root_inode.inode_id = ROOT_INODE_ID;
    root_inode.file_type = static_cast<uint8_t>(FileType::Directory);
    root_inode.mode = 0755;
    root_inode.size = 0;
    root_inode.blocks_count = 0;
    root_inode.atime = current_time_ns();
    root_inode.mtime = current_time_ns();
    root_inode.ctime = current_time_ns();
    root_inode.generation = 1;
    for (size_t i = 0; i < INODE_DIRECT_POINTERS; ++i) {
        root_inode.direct[i] = INVALID_BLOCK;
    }
    root_inode.indirect = INVALID_BLOCK;
    root_inode.double_indirect = INVALID_BLOCK;
    root_inode.update_checksum();

    // Write root inode to first block of inode table
    device.read_block(geo.inode_table_start, inode_buf.data());
    std::memcpy(inode_buf.data() + (ROOT_INODE_ID * sizeof(DiskInode)), &root_inode, sizeof(DiskInode));
    auto w_root = device.write_block(geo.inode_table_start, inode_buf.data());
    if (w_root.is_err()) return w_root;

    // Sync hardware storage
    return device.sync();
}

Result<void> FileSystem::mount() {
    std::unique_lock<std::shared_mutex> lock(fs_mutex_);
    if (is_mounted_) return Result<void>::err(FsError::DeviceAlreadyMounted);

    // 1. Load active superblock
    auto sb_load = superblock_mgr_.load(*device_);
    if (sb_load.is_err()) return sb_load;

    const auto& sb = superblock_mgr_.get();
    geometry_ = DiskGeometry::compute(sb.total_blocks);

    // 2. Load Bitmap Allocator
    allocator_ = std::make_unique<BitmapAllocator>(
        sb.bitmap_start_block, sb.bitmap_blocks_count, sb.total_blocks
    );
    auto alloc_res = allocator_->load(*device_);
    if (alloc_res.is_err()) return alloc_res;

    // 3. Load Refcount Table
    refcount_table_ = std::make_unique<RefCountTable>(
        sb.refcount_start_block, sb.refcount_blocks_count, sb.total_blocks
    );
    auto ref_res = refcount_table_->load(*device_);
    if (ref_res.is_err()) return ref_res;

    is_mounted_ = true;
    return Result<void>::ok();
}

Result<void> FileSystem::unmount() {
    std::unique_lock<std::shared_mutex> lock(fs_mutex_);
    if (!is_mounted_) return Result<void>::err(FsError::DeviceNotMounted);

    auto sync_res = sync();
    if (sync_res.is_err()) return sync_res;

    is_mounted_ = false;
    return Result<void>::ok();
}

Result<void> FileSystem::sync() {
    if (!is_mounted_) return Result<void>::err(FsError::DeviceNotMounted);

    // Flush dirty buffer pool pages
    auto buf_res = buffer_pool_->flush_all();
    if (buf_res.is_err()) return buf_res;

    // Flush allocator and refcounts
    auto alloc_res = allocator_->flush(*device_);
    if (alloc_res.is_err()) return alloc_res;

    auto ref_res = refcount_table_->flush(*device_);
    if (ref_res.is_err()) return ref_res;

    // Update and commit superblock
    SuperblockHeader sb = superblock_mgr_.get();
    sb.free_blocks_count = allocator_->get_free_blocks_count();
    sb.snapshot_count = static_cast<uint32_t>(snapshots_.size());

    return superblock_mgr_.commit(*device_, sb);
}

Result<DiskInode> FileSystem::read_inode(inode_id_t inode_id) {
    if (inode_id == 0 || inode_id >= geometry_.total_inodes) {
        return Result<DiskInode>::err(FsError::NotFound);
    }

    uint32_t block_offset = (inode_id * sizeof(DiskInode)) / BLOCK_SIZE;
    uint32_t entry_offset = (inode_id * sizeof(DiskInode)) % BLOCK_SIZE;
    block_id_t target_block = geometry_.inode_table_start + block_offset;

    AlignedBuffer buf(BLOCK_SIZE);
    auto res = buffer_pool_->read_block(target_block, buf.data());
    if (res.is_err()) return Result<DiskInode>::err(res.error());

    DiskInode inode;
    std::memcpy(&inode, buf.data() + entry_offset, sizeof(DiskInode));

    if (!inode.verify_checksum()) {
        return Result<DiskInode>::err(FsError::ChecksumMismatch);
    }

    return Result<DiskInode>::ok(inode);
}

Result<void> FileSystem::write_inode(const DiskInode& inode) {
    if (inode.inode_id == 0 || inode.inode_id >= geometry_.total_inodes) {
        return Result<void>::err(FsError::InvalidArg);
    }

    uint32_t block_offset = (inode.inode_id * sizeof(DiskInode)) / BLOCK_SIZE;
    uint32_t entry_offset = (inode.inode_id * sizeof(DiskInode)) % BLOCK_SIZE;
    block_id_t target_block = geometry_.inode_table_start + block_offset;

    DiskInode copy = inode;
    copy.generation++;
    copy.update_checksum();

    AlignedBuffer buf(BLOCK_SIZE);
    auto read_res = buffer_pool_->read_block(target_block, buf.data());
    if (read_res.is_err()) return read_res;

    std::memcpy(buf.data() + entry_offset, &copy, sizeof(DiskInode));
    return buffer_pool_->write_block(target_block, buf.data());
}

Result<inode_id_t> FileSystem::allocate_inode() {
    AlignedBuffer buf(BLOCK_SIZE);

    for (uint32_t b = 0; b < geometry_.inode_table_blocks; ++b) {
        block_id_t blk = geometry_.inode_table_start + b;
        auto res = buffer_pool_->read_block(blk, buf.data());
        if (res.is_err()) return Result<inode_id_t>::err(res.error());

        for (uint32_t i = 0; i < INODES_PER_BLOCK; ++i) {
            inode_id_t id = b * INODES_PER_BLOCK + i;
            if (id == 0) continue; // Inode 0 reserved/invalid

            const auto* existing = reinterpret_cast<const DiskInode*>(buf.data() + (i * sizeof(DiskInode)));
            if (existing->file_type == 0 || existing->inode_id == INVALID_INODE) {
                return Result<inode_id_t>::ok(id);
            }
        }
    }

    return Result<inode_id_t>::err(FsError::NoSpaceLeft);
}

Result<void> FileSystem::free_inode(inode_id_t inode_id) {
    auto inode_res = read_inode(inode_id);
    if (inode_res.is_err()) return Result<void>::err(inode_res.error());

    DiskInode inode = inode_res.value();
    auto free_blk_res = free_all_file_blocks(inode);
    if (free_blk_res.is_err()) return free_blk_res;

    inode.reset();
    inode.file_type = 0;
    inode.update_checksum();

    return write_inode(inode);
}

Result<block_id_t> FileSystem::get_or_allocate_file_block(
    DiskInode& inode, uint32_t logical_block_idx, bool for_write
) {
    // 1. Direct Blocks (0 .. 9)
    if (logical_block_idx < INODE_DIRECT_POINTERS) {
        block_id_t current = inode.direct[logical_block_idx];

        if (!for_write) {
            return Result<block_id_t>::ok(current);
        }

        // Writing: If block not allocated yet, allocate fresh block
        if (current == INVALID_BLOCK) {
            auto alloc_res = allocator_->allocate_block();
            if (alloc_res.is_err()) return alloc_res;

            block_id_t new_blk = alloc_res.value();
            refcount_table_->set_refcount(new_blk, 1);

            // Zero initialized
            AlignedBuffer zero_buf(BLOCK_SIZE);
            zero_buf.zero();
            buffer_pool_->write_block(new_blk, zero_buf.data());

            inode.direct[logical_block_idx] = new_blk;
            inode.blocks_count++;
            return Result<block_id_t>::ok(new_blk);
        }

        // Block is already allocated: Check if Copy-on-Write is required!
        uint16_t ref = refcount_table_->get_refcount(current).value_or(1);
        if (ref > 1) {
            // === COPY-ON-WRITE TRIGGER ===
            auto alloc_res = allocator_->allocate_block();
            if (alloc_res.is_err()) return alloc_res;

            block_id_t new_blk = alloc_res.value();
            refcount_table_->set_refcount(new_blk, 1);

            // Copy contents from old block to new block via page cache
            AlignedBuffer copy_buf(BLOCK_SIZE);
            buffer_pool_->read_block(current, copy_buf.data());
            buffer_pool_->write_block(new_blk, copy_buf.data());

            // Decrement old block refcount
            refcount_table_->decrement(current);

            // Repoint inode to new physical block
            inode.direct[logical_block_idx] = new_blk;
            return Result<block_id_t>::ok(new_blk);
        }

        return Result<block_id_t>::ok(current);
    }

    // 2. Single Indirect Blocks (10 .. 10 + 1023)
    uint32_t indirect_idx = logical_block_idx - INODE_DIRECT_POINTERS;
    if (indirect_idx < POINTERS_PER_BLOCK) {
        if (inode.indirect == INVALID_BLOCK) {
            if (!for_write) return Result<block_id_t>::ok(INVALID_BLOCK);

            auto alloc_res = allocator_->allocate_block();
            if (alloc_res.is_err()) return alloc_res;

            inode.indirect = alloc_res.value();
            refcount_table_->set_refcount(inode.indirect, 1);

            AlignedBuffer zero_buf(BLOCK_SIZE);
            auto* ptrs = reinterpret_cast<block_id_t*>(zero_buf.data());
            for (size_t p = 0; p < POINTERS_PER_BLOCK; ++p) ptrs[p] = INVALID_BLOCK;
            buffer_pool_->write_block(inode.indirect, zero_buf.data());
        } else if (for_write && refcount_table_->is_shared(inode.indirect)) {
            // CoW on indirect block itself
            auto alloc_res = allocator_->allocate_block();
            if (alloc_res.is_err()) return alloc_res;

            block_id_t new_ind = alloc_res.value();
            refcount_table_->set_refcount(new_ind, 1);

            AlignedBuffer copy_buf(BLOCK_SIZE);
            buffer_pool_->read_block(inode.indirect, copy_buf.data());
            buffer_pool_->write_block(new_ind, copy_buf.data());

            refcount_table_->decrement(inode.indirect);
            inode.indirect = new_ind;
        }

        // Read indirect block
        AlignedBuffer ind_buf(BLOCK_SIZE);
        buffer_pool_->read_block(inode.indirect, ind_buf.data());
        auto* ptrs = reinterpret_cast<block_id_t*>(ind_buf.data());

        block_id_t current = ptrs[indirect_idx];
        if (!for_write) return Result<block_id_t>::ok(current);

        if (current == INVALID_BLOCK) {
            auto alloc_res = allocator_->allocate_block();
            if (alloc_res.is_err()) return alloc_res;

            block_id_t new_blk = alloc_res.value();
            refcount_table_->set_refcount(new_blk, 1);

            AlignedBuffer zero_buf(BLOCK_SIZE);
            zero_buf.zero();
            buffer_pool_->write_block(new_blk, zero_buf.data());

            ptrs[indirect_idx] = new_blk;
            buffer_pool_->write_block(inode.indirect, ind_buf.data());
            inode.blocks_count++;
            return Result<block_id_t>::ok(new_blk);
        }

        uint16_t ref = refcount_table_->get_refcount(current).value_or(1);
        if (ref > 1) {
            // === COPY-ON-WRITE TRIGGER ===
            auto alloc_res = allocator_->allocate_block();
            if (alloc_res.is_err()) return alloc_res;

            block_id_t new_blk = alloc_res.value();
            refcount_table_->set_refcount(new_blk, 1);

            AlignedBuffer copy_buf(BLOCK_SIZE);
            buffer_pool_->read_block(current, copy_buf.data());
            buffer_pool_->write_block(new_blk, copy_buf.data());

            refcount_table_->decrement(current);
            ptrs[indirect_idx] = new_blk;
            buffer_pool_->write_block(inode.indirect, ind_buf.data());
            return Result<block_id_t>::ok(new_blk);
        }

        return Result<block_id_t>::ok(current);
    }

    return Result<block_id_t>::err(FsError::FileTooLarge);
}

Result<void> FileSystem::free_all_file_blocks(DiskInode& inode) {
    // Free direct blocks
    for (size_t i = 0; i < INODE_DIRECT_POINTERS; ++i) {
        if (inode.direct[i] != INVALID_BLOCK) {
            auto dec_res = refcount_table_->decrement(inode.direct[i]);
            if (dec_res.is_ok() && dec_res.value() == 0) {
                allocator_->free_block(inode.direct[i]);
                buffer_pool_->invalidate_block(inode.direct[i]);
            }
            inode.direct[i] = INVALID_BLOCK;
        }
    }

    // Free indirect blocks
    if (inode.indirect != INVALID_BLOCK) {
        AlignedBuffer ind_buf(BLOCK_SIZE);
        if (buffer_pool_->read_block(inode.indirect, ind_buf.data()).is_ok()) {
            auto* ptrs = reinterpret_cast<block_id_t*>(ind_buf.data());
            for (size_t p = 0; p < POINTERS_PER_BLOCK; ++p) {
                if (ptrs[p] != INVALID_BLOCK) {
                    auto dec_res = refcount_table_->decrement(ptrs[p]);
                    if (dec_res.is_ok() && dec_res.value() == 0) {
                        allocator_->free_block(ptrs[p]);
                        buffer_pool_->invalidate_block(ptrs[p]);
                    }
                }
            }
        }
        auto ind_dec = refcount_table_->decrement(inode.indirect);
        if (ind_dec.is_ok() && ind_dec.value() == 0) {
            allocator_->free_block(inode.indirect);
            buffer_pool_->invalidate_block(inode.indirect);
        }
        inode.indirect = INVALID_BLOCK;
    }

    inode.size = 0;
    inode.blocks_count = 0;
    return Result<void>::ok();
}

Result<size_t> FileSystem::read_file(
    inode_id_t inode_id, uint64_t offset, uint8_t* buffer, size_t count
) {
    if (!is_mounted_) return Result<size_t>::err(FsError::DeviceNotMounted);
    if (!buffer) return Result<size_t>::err(FsError::InvalidArg);

    auto inode_res = read_inode(inode_id);
    if (inode_res.is_err()) return Result<size_t>::err(inode_res.error());
    DiskInode inode = inode_res.value();

    if (offset >= inode.size) return Result<size_t>::ok(0);

    size_t bytes_to_read = std::min(static_cast<uint64_t>(count), inode.size - offset);
    size_t bytes_read = 0;
    AlignedBuffer block_buf(BLOCK_SIZE);

    while (bytes_read < bytes_to_read) {
        uint64_t current_offset = offset + bytes_read;
        uint32_t logical_idx = static_cast<uint32_t>(current_offset / BLOCK_SIZE);
        size_t block_offset = current_offset % BLOCK_SIZE;
        size_t chunk = std::min(BLOCK_SIZE - block_offset, bytes_to_read - bytes_read);

        auto blk_res = get_or_allocate_file_block(inode, logical_idx, false);
        if (blk_res.is_err()) return Result<size_t>::err(blk_res.error());

        block_id_t physical_blk = blk_res.value();
        if (physical_blk == INVALID_BLOCK) {
            std::memset(buffer + bytes_read, 0, chunk); // sparse hole
        } else {
            auto read_res = buffer_pool_->read_block(physical_blk, block_buf.data());
            if (read_res.is_err()) return Result<size_t>::err(read_res.error());
            std::memcpy(buffer + bytes_read, block_buf.data() + block_offset, chunk);
        }

        bytes_read += chunk;
    }

    inode.atime = current_time_ns();
    write_inode(inode);

    return Result<size_t>::ok(bytes_read);
}

Result<size_t> FileSystem::write_file(
    inode_id_t inode_id, uint64_t offset, const uint8_t* buffer, size_t count
) {
    if (!is_mounted_) return Result<size_t>::err(FsError::DeviceNotMounted);
    if (!buffer || count == 0) return Result<size_t>::ok(0);

    auto inode_res = read_inode(inode_id);
    if (inode_res.is_err()) return Result<size_t>::err(inode_res.error());
    DiskInode inode = inode_res.value();

    if (inode.file_type == static_cast<uint8_t>(FileType::Directory)) {
        return Result<size_t>::err(FsError::IsADirectory);
    }

    size_t bytes_written = 0;
    AlignedBuffer block_buf(BLOCK_SIZE);

    while (bytes_written < count) {
        uint64_t current_offset = offset + bytes_written;
        uint32_t logical_idx = static_cast<uint32_t>(current_offset / BLOCK_SIZE);
        size_t block_offset = current_offset % BLOCK_SIZE;
        size_t chunk = std::min(BLOCK_SIZE - block_offset, count - bytes_written);

        auto blk_res = get_or_allocate_file_block(inode, logical_idx, true);
        if (blk_res.is_err()) return Result<size_t>::err(blk_res.error());

        block_id_t physical_blk = blk_res.value();

        // If partial block write, read existing data first
        if (chunk < BLOCK_SIZE) {
            buffer_pool_->read_block(physical_blk, block_buf.data());
        }

        std::memcpy(block_buf.data() + block_offset, buffer + bytes_written, chunk);
        auto write_res = buffer_pool_->write_block(physical_blk, block_buf.data());
        if (write_res.is_err()) return Result<size_t>::err(write_res.error());

        bytes_written += chunk;
    }

    if (offset + bytes_written > inode.size) {
        inode.size = offset + bytes_written;
    }
    inode.mtime = current_time_ns();
    inode.ctime = current_time_ns();
    inode.generation++;

    auto w_inode_res = write_inode(inode);
    if (w_inode_res.is_err()) return Result<size_t>::err(w_inode_res.error());

    device_->track_user_write(bytes_written);
    return Result<size_t>::ok(bytes_written);
}

Result<void> FileSystem::truncate_file(inode_id_t inode_id, uint64_t new_size) {
    auto inode_res = read_inode(inode_id);
    if (inode_res.is_err()) return Result<void>::err(inode_res.error());
    DiskInode inode = inode_res.value();

    if (new_size == 0) {
        free_all_file_blocks(inode);
    } else {
        inode.size = new_size;
    }

    inode.mtime = current_time_ns();
    inode.ctime = current_time_ns();
    return write_inode(inode);
}

Result<FileStat> FileSystem::stat_file(inode_id_t inode_id) {
    auto inode_res = read_inode(inode_id);
    if (inode_res.is_err()) return Result<FileStat>::err(inode_res.error());
    const DiskInode& inode = inode_res.value();

    FileStat st;
    st.inode_id = inode.inode_id;
    st.type = static_cast<FileType>(inode.file_type);
    st.size = inode.size;
    st.blocks_count = inode.blocks_count;
    st.mode = inode.mode;
    st.atime = inode.atime;
    st.mtime = inode.mtime;
    st.ctime = inode.ctime;
    st.generation = inode.generation;

    for (size_t i = 0; i < INODE_DIRECT_POINTERS; ++i) {
        if (inode.direct[i] != INVALID_BLOCK) {
            st.allocated_blocks.push_back(inode.direct[i]);
        }
    }
    if (inode.indirect != INVALID_BLOCK) {
        AlignedBuffer ind_buf(BLOCK_SIZE);
        if (buffer_pool_->read_block(inode.indirect, ind_buf.data()).is_ok()) {
            auto* ptrs = reinterpret_cast<block_id_t*>(ind_buf.data());
            for (size_t p = 0; p < POINTERS_PER_BLOCK; ++p) {
                if (ptrs[p] != INVALID_BLOCK) {
                    st.allocated_blocks.push_back(ptrs[p]);
                }
            }
        }
    }

    return Result<FileStat>::ok(st);
}

// Directory Helpers
Result<std::vector<DirEntry>> FileSystem::dir_get_all_entries(DiskInode& dir_inode) {
    std::vector<DirEntry> entries;
    size_t total_possible_entries = (dir_inode.size + sizeof(DirEntry) - 1) / sizeof(DirEntry);
    AlignedBuffer block_buf(BLOCK_SIZE);

    for (size_t i = 0; i < total_possible_entries; i += DIR_ENTRIES_PER_BLOCK) {
        uint32_t logical_idx = static_cast<uint32_t>(i / DIR_ENTRIES_PER_BLOCK);
        auto blk_res = get_or_allocate_file_block(dir_inode, logical_idx, false);
        if (blk_res.is_err() || blk_res.value() == INVALID_BLOCK) continue;

        auto read_res = buffer_pool_->read_block(blk_res.value(), block_buf.data());
        if (read_res.is_err()) continue;

        const auto* dir_ptrs = reinterpret_cast<const DirEntry*>(block_buf.data());
        size_t count_in_block = std::min(DIR_ENTRIES_PER_BLOCK, total_possible_entries - i);
        for (size_t j = 0; j < count_in_block; ++j) {
            if (dir_ptrs[j].is_valid()) {
                entries.push_back(dir_ptrs[j]);
            }
        }
    }

    return Result<std::vector<DirEntry>>::ok(entries);
}

Result<void> FileSystem::dir_add_entry(DiskInode& dir_inode, const DirEntry& entry) {
    AlignedBuffer block_buf(BLOCK_SIZE);
    size_t total_entries = dir_inode.size / sizeof(DirEntry);

    // Look for an unused/deleted slot first
    for (size_t i = 0; i < total_entries; i += DIR_ENTRIES_PER_BLOCK) {
        uint32_t logical_idx = static_cast<uint32_t>(i / DIR_ENTRIES_PER_BLOCK);
        auto blk_res = get_or_allocate_file_block(dir_inode, logical_idx, false);
        if (blk_res.is_err() || blk_res.value() == INVALID_BLOCK) continue;

        buffer_pool_->read_block(blk_res.value(), block_buf.data());
        auto* dir_ptrs = reinterpret_cast<DirEntry*>(block_buf.data());
        size_t count = std::min(DIR_ENTRIES_PER_BLOCK, total_entries - i);

        for (size_t j = 0; j < count; ++j) {
            if (!dir_ptrs[j].is_valid()) {
                // Reuse free slot!
                dir_ptrs[j] = entry;
                buffer_pool_->write_block(blk_res.value(), block_buf.data());
                dir_inode.mtime = current_time_ns();
                return write_inode(dir_inode);
            }
        }
    }

    // Append to end of directory
    uint32_t logical_idx = static_cast<uint32_t>(total_entries / DIR_ENTRIES_PER_BLOCK);
    size_t slot_idx = total_entries % DIR_ENTRIES_PER_BLOCK;

    auto blk_res = get_or_allocate_file_block(dir_inode, logical_idx, true);
    if (blk_res.is_err()) return Result<void>::err(blk_res.error());

    buffer_pool_->read_block(blk_res.value(), block_buf.data());
    auto* dir_ptrs = reinterpret_cast<DirEntry*>(block_buf.data());
    dir_ptrs[slot_idx] = entry;
    buffer_pool_->write_block(blk_res.value(), block_buf.data());

    dir_inode.size += sizeof(DirEntry);
    dir_inode.mtime = current_time_ns();
    return write_inode(dir_inode);
}

Result<void> FileSystem::dir_remove_entry(DiskInode& dir_inode, std::string_view name) {
    AlignedBuffer block_buf(BLOCK_SIZE);
    size_t total_entries = dir_inode.size / sizeof(DirEntry);

    for (size_t i = 0; i < total_entries; i += DIR_ENTRIES_PER_BLOCK) {
        uint32_t logical_idx = static_cast<uint32_t>(i / DIR_ENTRIES_PER_BLOCK);
        auto blk_res = get_or_allocate_file_block(dir_inode, logical_idx, false);
        if (blk_res.is_err() || blk_res.value() == INVALID_BLOCK) continue;

        buffer_pool_->read_block(blk_res.value(), block_buf.data());
        auto* dir_ptrs = reinterpret_cast<DirEntry*>(block_buf.data());
        size_t count = std::min(DIR_ENTRIES_PER_BLOCK, total_entries - i);

        for (size_t j = 0; j < count; ++j) {
            if (dir_ptrs[j].is_valid() && dir_ptrs[j].get_name() == name) {
                // Invalidate slot
                dir_ptrs[j].inode_id = INVALID_INODE;
                dir_ptrs[j].name_len = 0;
                buffer_pool_->write_block(blk_res.value(), block_buf.data());
                dir_inode.mtime = current_time_ns();
                return write_inode(dir_inode);
            }
        }
    }

    return Result<void>::err(FsError::NotFound);
}

Result<DirEntry> FileSystem::dir_find_entry(DiskInode& dir_inode, std::string_view name) {
    AlignedBuffer block_buf(BLOCK_SIZE);
    size_t total_entries = dir_inode.size / sizeof(DirEntry);

    for (size_t i = 0; i < total_entries; i += DIR_ENTRIES_PER_BLOCK) {
        uint32_t logical_idx = static_cast<uint32_t>(i / DIR_ENTRIES_PER_BLOCK);
        auto blk_res = get_or_allocate_file_block(dir_inode, logical_idx, false);
        if (blk_res.is_err() || blk_res.value() == INVALID_BLOCK) continue;

        buffer_pool_->read_block(blk_res.value(), block_buf.data());
        const auto* dir_ptrs = reinterpret_cast<const DirEntry*>(block_buf.data());
        size_t count = std::min(DIR_ENTRIES_PER_BLOCK, total_entries - i);

        for (size_t j = 0; j < count; ++j) {
            if (dir_ptrs[j].is_valid() && dir_ptrs[j].get_name() == name) {
                return Result<DirEntry>::ok(dir_ptrs[j]);
            }
        }
    }

    return Result<DirEntry>::err(FsError::NotFound);
}

Result<inode_id_t> FileSystem::resolve_path_internal(const std::string& path) {
    if (path.empty() || path == "/") {
        return Result<inode_id_t>::ok(ROOT_INODE_ID);
    }

    std::stringstream ss(path);
    std::string token;
    inode_id_t current_id = ROOT_INODE_ID;

    while (std::getline(ss, token, '/')) {
        if (token.empty() || token == ".") continue;

        auto inode_res = read_inode(current_id);
        if (inode_res.is_err()) return Result<inode_id_t>::err(inode_res.error());
        DiskInode current_inode = inode_res.value();

        if (current_inode.file_type != static_cast<uint8_t>(FileType::Directory)) {
            return Result<inode_id_t>::err(FsError::NotADirectory);
        }

        auto entry_res = dir_find_entry(current_inode, token);
        if (entry_res.is_err()) return Result<inode_id_t>::err(FsError::NotFound);

        current_id = entry_res.value().inode_id;
    }

    return Result<inode_id_t>::ok(current_id);
}

Result<inode_id_t> FileSystem::lookup_path(const std::string& path) {
    return resolve_path_internal(path);
}

Result<std::pair<inode_id_t, std::string>> FileSystem::resolve_parent_and_basename(const std::string& path) {
    if (path.empty() || path == "/") {
        return Result<std::pair<inode_id_t, std::string>>::err(FsError::InvalidArg);
    }

    size_t last_slash = path.find_last_of('/');
    std::string parent_path = (last_slash == std::string::npos || last_slash == 0) ? "/" : path.substr(0, last_slash);
    std::string basename = (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);

    if (basename.empty()) {
        return Result<std::pair<inode_id_t, std::string>>::err(FsError::InvalidArg);
    }

    auto parent_res = resolve_path_internal(parent_path);
    if (parent_res.is_err()) return Result<std::pair<inode_id_t, std::string>>::err(parent_res.error());

    return Result<std::pair<inode_id_t, std::string>>::ok({parent_res.value(), basename});
}

Result<inode_id_t> FileSystem::create_file(const std::string& path, FileType type, uint16_t mode) {
    if (!is_mounted_) return Result<inode_id_t>::err(FsError::DeviceNotMounted);

    auto parent_info = resolve_parent_and_basename(path);
    if (parent_info.is_err()) return Result<inode_id_t>::err(parent_info.error());

    auto [parent_id, basename] = parent_info.value();
    auto parent_inode_res = read_inode(parent_id);
    if (parent_inode_res.is_err()) return Result<inode_id_t>::err(parent_inode_res.error());
    DiskInode parent_inode = parent_inode_res.value();

    // Check if entry already exists
    if (dir_find_entry(parent_inode, basename).is_ok()) {
        return Result<inode_id_t>::err(FsError::AlreadyExists);
    }

    // Allocate new inode
    auto new_id_res = allocate_inode();
    if (new_id_res.is_err()) return new_id_res;
    inode_id_t new_id = new_id_res.value();

    DiskInode new_inode;
    new_inode.reset();
    new_inode.inode_id = new_id;
    new_inode.file_type = static_cast<uint8_t>(type);
    new_inode.mode = mode;
    new_inode.size = 0;
    new_inode.blocks_count = 0;
    new_inode.atime = current_time_ns();
    new_inode.mtime = current_time_ns();
    new_inode.ctime = current_time_ns();
    new_inode.generation = 1;
    for (size_t i = 0; i < INODE_DIRECT_POINTERS; ++i) new_inode.direct[i] = INVALID_BLOCK;
    new_inode.indirect = INVALID_BLOCK;
    new_inode.double_indirect = INVALID_BLOCK;
    new_inode.update_checksum();

    auto write_res = write_inode(new_inode);
    if (write_res.is_err()) return Result<inode_id_t>::err(write_res.error());

    // Add to parent directory
    DirEntry entry;
    entry.inode_id = new_id;
    entry.file_type = static_cast<uint8_t>(type);
    entry.set_name(basename);

    auto add_res = dir_add_entry(parent_inode, entry);
    if (add_res.is_err()) {
        free_inode(new_id);
        return Result<inode_id_t>::err(add_res.error());
    }

    return Result<inode_id_t>::ok(new_id);
}

Result<void> FileSystem::remove_file(const std::string& path) {
    if (!is_mounted_) return Result<void>::err(FsError::DeviceNotMounted);

    auto parent_info = resolve_parent_and_basename(path);
    if (parent_info.is_err()) return Result<void>::err(parent_info.error());

    auto [parent_id, basename] = parent_info.value();
    auto parent_inode_res = read_inode(parent_id);
    if (parent_inode_res.is_err()) return Result<void>::err(parent_inode_res.error());
    DiskInode parent_inode = parent_inode_res.value();

    auto entry_res = dir_find_entry(parent_inode, basename);
    if (entry_res.is_err()) return Result<void>::err(FsError::NotFound);

    if (entry_res.value().file_type == static_cast<uint8_t>(FileType::Directory)) {
        return Result<void>::err(FsError::IsADirectory);
    }

    inode_id_t file_id = entry_res.value().inode_id;
    dir_remove_entry(parent_inode, basename);
    return free_inode(file_id);
}

Result<void> FileSystem::create_directory(const std::string& path, uint16_t mode) {
    auto res = create_file(path, FileType::Directory, mode);
    if (res.is_err()) return Result<void>::err(res.error());
    return Result<void>::ok();
}

Result<std::vector<DirEntry>> FileSystem::list_directory(const std::string& path) {
    auto id_res = resolve_path_internal(path);
    if (id_res.is_err()) return Result<std::vector<DirEntry>>::err(id_res.error());

    auto inode_res = read_inode(id_res.value());
    if (inode_res.is_err()) return Result<std::vector<DirEntry>>::err(inode_res.error());
    DiskInode dir_inode = inode_res.value();

    if (dir_inode.file_type != static_cast<uint8_t>(FileType::Directory)) {
        return Result<std::vector<DirEntry>>::err(FsError::NotADirectory);
    }

    return dir_get_all_entries(dir_inode);
}

Result<void> FileSystem::remove_directory(const std::string& path) {
    if (path == "/" || path.empty()) {
        return Result<void>::err(FsError::InvalidArg);
    }

    auto entries_res = list_directory(path);
    if (entries_res.is_err()) return Result<void>::err(entries_res.error());
    if (!entries_res.value().empty()) {
        return Result<void>::err(FsError::DirectoryNotEmpty);
    }

    auto parent_info = resolve_parent_and_basename(path);
    if (parent_info.is_err()) return Result<void>::err(parent_info.error());

    auto [parent_id, basename] = parent_info.value();
    auto parent_inode_res = read_inode(parent_id);
    if (parent_inode_res.is_err()) return Result<void>::err(parent_inode_res.error());
    DiskInode parent_inode = parent_inode_res.value();

    auto entry_res = dir_find_entry(parent_inode, basename);
    if (entry_res.is_err()) return Result<void>::err(FsError::NotFound);

    inode_id_t dir_id = entry_res.value().inode_id;
    dir_remove_entry(parent_inode, basename);
    return free_inode(dir_id);
}

// =========================================================================
// COPY-ON-WRITE INSTANT FILE CLONING (cp src dst)
// =========================================================================
Result<inode_id_t> FileSystem::clone_file(const std::string& src_path, const std::string& dst_path) {
    if (!is_mounted_) return Result<inode_id_t>::err(FsError::DeviceNotMounted);

    auto src_id_res = resolve_path_internal(src_path);
    if (src_id_res.is_err()) return Result<inode_id_t>::err(src_id_res.error());

    auto src_inode_res = read_inode(src_id_res.value());
    if (src_inode_res.is_err()) return Result<inode_id_t>::err(src_inode_res.error());
    const DiskInode& src_inode = src_inode_res.value();

    if (src_inode.file_type != static_cast<uint8_t>(FileType::Regular)) {
        return Result<inode_id_t>::err(FsError::InvalidArg);
    }

    auto parent_info = resolve_parent_and_basename(dst_path);
    if (parent_info.is_err()) return Result<inode_id_t>::err(parent_info.error());

    auto [parent_id, basename] = parent_info.value();
    auto parent_inode_res = read_inode(parent_id);
    if (parent_inode_res.is_err()) return Result<inode_id_t>::err(parent_inode_res.error());
    DiskInode parent_inode = parent_inode_res.value();

    if (dir_find_entry(parent_inode, basename).is_ok()) {
        return Result<inode_id_t>::err(FsError::AlreadyExists);
    }

    auto new_id_res = allocate_inode();
    if (new_id_res.is_err()) return new_id_res;
    inode_id_t new_id = new_id_res.value();

    // Create cloned inode referencing identical data blocks!
    DiskInode cloned_inode = src_inode;
    cloned_inode.inode_id = new_id;
    cloned_inode.ctime = current_time_ns();
    cloned_inode.mtime = current_time_ns();
    cloned_inode.atime = current_time_ns();
    cloned_inode.generation = 1;

    // Increment reference counts for all shared direct blocks
    for (size_t i = 0; i < INODE_DIRECT_POINTERS; ++i) {
        if (cloned_inode.direct[i] != INVALID_BLOCK) {
            refcount_table_->increment(cloned_inode.direct[i]);
        }
    }

    // Increment reference count for indirect block
    if (cloned_inode.indirect != INVALID_BLOCK) {
        refcount_table_->increment(cloned_inode.indirect);
        AlignedBuffer ind_buf(BLOCK_SIZE);
        if (buffer_pool_->read_block(cloned_inode.indirect, ind_buf.data()).is_ok()) {
            auto* ptrs = reinterpret_cast<block_id_t*>(ind_buf.data());
            for (size_t p = 0; p < POINTERS_PER_BLOCK; ++p) {
                if (ptrs[p] != INVALID_BLOCK) {
                    refcount_table_->increment(ptrs[p]);
                }
            }
        }
    }

    cloned_inode.update_checksum();
    write_inode(cloned_inode);

    DirEntry entry;
    entry.inode_id = new_id;
    entry.file_type = static_cast<uint8_t>(FileType::Regular);
    entry.set_name(basename);
    dir_add_entry(parent_inode, entry);

    return Result<inode_id_t>::ok(new_id);
}

// =========================================================================
// SNAPSHOTS (O(1) Instantaneous Copy-on-Write Volume Snapshots)
// =========================================================================
Result<inode_id_t> FileSystem::clone_inode_tree(inode_id_t src_root_id) {
    auto src_res = read_inode(src_root_id);
    if (src_res.is_err()) return Result<inode_id_t>::err(src_res.error());
    DiskInode src_inode = src_res.value();

    auto new_id_res = allocate_inode();
    if (new_id_res.is_err()) return new_id_res;
    inode_id_t new_id = new_id_res.value();

    DiskInode cloned;
    cloned.reset();
    cloned.inode_id = new_id;
    cloned.file_type = src_inode.file_type;
    cloned.mode = src_inode.mode;
    cloned.uid = src_inode.uid;
    cloned.gid = src_inode.gid;
    cloned.atime = current_time_ns();
    cloned.mtime = current_time_ns();
    cloned.ctime = current_time_ns();
    cloned.generation = 1;

    if (src_inode.file_type == static_cast<uint8_t>(FileType::Regular)) {
        cloned.size = src_inode.size;
        cloned.blocks_count = src_inode.blocks_count;
        for (size_t i = 0; i < INODE_DIRECT_POINTERS; ++i) {
            cloned.direct[i] = src_inode.direct[i];
            if (cloned.direct[i] != INVALID_BLOCK) {
                refcount_table_->increment(cloned.direct[i]);
            }
        }
        if (src_inode.indirect != INVALID_BLOCK) {
            cloned.indirect = src_inode.indirect;
            refcount_table_->increment(cloned.indirect);
            AlignedBuffer ind_buf(BLOCK_SIZE);
            if (buffer_pool_->read_block(cloned.indirect, ind_buf.data()).is_ok()) {
                auto* ptrs = reinterpret_cast<block_id_t*>(ind_buf.data());
                for (size_t p = 0; p < POINTERS_PER_BLOCK; ++p) {
                    if (ptrs[p] != INVALID_BLOCK) {
                        refcount_table_->increment(ptrs[p]);
                    }
                }
            }
        }
        cloned.update_checksum();
        write_inode(cloned);
        return Result<inode_id_t>::ok(new_id);
    } else if (src_inode.file_type == static_cast<uint8_t>(FileType::Directory)) {
        cloned.size = 0;
        cloned.blocks_count = 0;
        cloned.update_checksum();
        write_inode(cloned);

        auto entries = dir_get_all_entries(src_inode).value_or(std::vector<DirEntry>{});
        for (const auto& entry : entries) {
            auto child_clone_res = clone_inode_tree(entry.inode_id);
            if (child_clone_res.is_ok()) {
                DirEntry new_entry;
                new_entry.inode_id = child_clone_res.value();
                new_entry.file_type = entry.file_type;
                new_entry.set_name(entry.get_name());
                dir_add_entry(cloned, new_entry);
            }
        }
        return Result<inode_id_t>::ok(new_id);
    }

    cloned.update_checksum();
    write_inode(cloned);
    return Result<inode_id_t>::ok(new_id);
}

Result<void> FileSystem::free_inode_tree(inode_id_t inode_id) {
    auto inode_res = read_inode(inode_id);
    if (inode_res.is_err()) return Result<void>::err(inode_res.error());
    DiskInode inode = inode_res.value();

    if (inode.file_type == static_cast<uint8_t>(FileType::Directory)) {
        auto entries = dir_get_all_entries(inode).value_or(std::vector<DirEntry>{});
        for (const auto& entry : entries) {
            free_inode_tree(entry.inode_id);
        }
    }
    return free_inode(inode_id);
}

Result<void> FileSystem::create_snapshot(const std::string& name) {
    if (!is_mounted_) return Result<void>::err(FsError::DeviceNotMounted);
    if (name.empty()) return Result<void>::err(FsError::InvalidArg);

    for (const auto& s : snapshots_) {
        if (s.name == name) return Result<void>::err(FsError::SnapshotAlreadyExists);
    }

    // Flush dirty buffers to guarantee consistent point-in-time image
    buffer_pool_->flush_all();

    auto cloned_root = clone_inode_tree(ROOT_INODE_ID);
    if (cloned_root.is_err()) return Result<void>::err(cloned_root.error());

    SnapshotInfo info;
    info.name = name;
    info.timestamp = current_time_ns();
    info.generation = superblock_mgr_.get().active_generation;
    info.root_inode_id = cloned_root.value();

    snapshots_.push_back(info);
    return sync();
}

Result<void> FileSystem::restore_snapshot(const std::string& name) {
    if (!is_mounted_) return Result<void>::err(FsError::DeviceNotMounted);

    auto it = std::find_if(snapshots_.begin(), snapshots_.end(), [&](const SnapshotInfo& s) {
        return s.name == name;
    });

    if (it == snapshots_.end()) {
        return Result<void>::err(FsError::SnapshotNotFound);
    }

    buffer_pool_->flush_all();

    // 1. Read existing active root inode and free all its children
    auto active_root_res = read_inode(ROOT_INODE_ID);
    if (active_root_res.is_err()) return Result<void>::err(active_root_res.error());
    DiskInode active_root = active_root_res.value();

    auto active_entries = dir_get_all_entries(active_root).value_or(std::vector<DirEntry>{});
    for (const auto& entry : active_entries) {
        free_inode_tree(entry.inode_id);
    }
    free_all_file_blocks(active_root);
    active_root.size = 0;
    active_root.blocks_count = 0;
    active_root.generation++;
    active_root.update_checksum();
    write_inode(active_root);

    // 2. Clone snapshot's entries back into active root
    auto snap_root_res = read_inode(it->root_inode_id);
    if (snap_root_res.is_err()) return Result<void>::err(snap_root_res.error());
    DiskInode snap_root = snap_root_res.value();

    auto snap_entries = dir_get_all_entries(snap_root).value_or(std::vector<DirEntry>{});
    for (const auto& entry : snap_entries) {
        auto child_clone_res = clone_inode_tree(entry.inode_id);
        if (child_clone_res.is_ok()) {
            DirEntry new_entry;
            new_entry.inode_id = child_clone_res.value();
            new_entry.file_type = entry.file_type;
            new_entry.set_name(entry.get_name());
            dir_add_entry(active_root, new_entry);
        }
    }

    return sync();
}

Result<std::vector<SnapshotInfo>> FileSystem::list_snapshots() {
    return Result<std::vector<SnapshotInfo>>::ok(snapshots_);
}

Result<void> FileSystem::delete_snapshot(const std::string& name) {
    auto it = std::find_if(snapshots_.begin(), snapshots_.end(), [&](const SnapshotInfo& s) {
        return s.name == name;
    });

    if (it == snapshots_.end()) {
        return Result<void>::err(FsError::SnapshotNotFound);
    }

    free_inode_tree(it->root_inode_id);
    snapshots_.erase(it);

    return sync();
}

// =========================================================================
// BLOCK-LEVEL DEDUPLICATION PASS
// =========================================================================
Result<size_t> FileSystem::deduplicate_blocks() {
    if (!is_mounted_) return Result<size_t>::err(FsError::DeviceNotMounted);

    // Map: CRC32 of block data -> canonical block_id
    std::unordered_map<uint32_t, block_id_t> hash_to_block;
    size_t blocks_deduplicated = 0;
    AlignedBuffer buf_a(BLOCK_SIZE);
    AlignedBuffer buf_b(BLOCK_SIZE);

    // Scan all inodes
    for (inode_id_t id = 1; id < geometry_.total_inodes; ++id) {
        auto inode_res = read_inode(id);
        if (inode_res.is_err()) continue;
        DiskInode inode = inode_res.value();

        if (inode.file_type != static_cast<uint8_t>(FileType::Regular)) continue;

        bool inode_modified = false;

        // Check direct blocks
        for (size_t i = 0; i < INODE_DIRECT_POINTERS; ++i) {
            block_id_t blk = inode.direct[i];
            if (blk == INVALID_BLOCK) continue;

            buffer_pool_->read_block(blk, buf_a.data());
            uint32_t crc = CRC32::calculate(buf_a.data(), BLOCK_SIZE);

            auto it = hash_to_block.find(crc);
            if (it == hash_to_block.end()) {
                hash_to_block[crc] = blk;
            } else if (it->second != blk) {
                // Verify identical bytes
                buffer_pool_->read_block(it->second, buf_b.data());
                if (std::memcmp(buf_a.data(), buf_b.data(), BLOCK_SIZE) == 0) {
                    // Merge duplicate!
                    block_id_t canonical = it->second;
                    refcount_table_->increment(canonical);

                    auto dec_res = refcount_table_->decrement(blk);
                    if (dec_res.is_ok() && dec_res.value() == 0) {
                        allocator_->free_block(blk);
                        buffer_pool_->invalidate_block(blk);
                    }

                    inode.direct[i] = canonical;
                    inode_modified = true;
                    blocks_deduplicated++;
                }
            }
        }

        if (inode_modified) {
            write_inode(inode);
        }
    }

    if (blocks_deduplicated > 0) {
        sync();
    }

    return Result<size_t>::ok(blocks_deduplicated);
}

FsStatus FileSystem::get_status() {
    FsStatus st;
    st.total_blocks = geometry_.total_blocks;
    st.free_blocks = allocator_ ? allocator_->get_free_blocks_count() : 0;
    st.used_blocks = (st.total_blocks >= st.free_blocks) ? (st.total_blocks - st.free_blocks) : 0;
    st.total_inodes = geometry_.total_inodes;
    st.free_inodes = 0;
    st.used_inodes = 0;
    st.active_generation = superblock_mgr_.get().active_generation;
    st.snapshot_count = static_cast<uint32_t>(snapshots_.size());

    st.block_utilization_pct = (st.total_blocks > 0) 
        ? (static_cast<double>(st.used_blocks) / static_cast<double>(st.total_blocks) * 100.0) : 0.0;
    st.cache_hit_ratio = buffer_pool_ ? buffer_pool_->get_stats().hit_ratio() : 0.0;
    st.write_amplification = device_ ? device_->get_stats().write_amplification_factor() : 1.0;

    return st;
}

Result<DiskInode> FileSystem::get_raw_inode(inode_id_t inode_id) {
    return read_inode(inode_id);
}

Result<uint16_t> FileSystem::get_block_refcount(block_id_t block_id) {
    if (!refcount_table_) return Result<uint16_t>::err(FsError::DeviceNotMounted);
    return refcount_table_->get_refcount(block_id);
}

} // namespace cowfs
