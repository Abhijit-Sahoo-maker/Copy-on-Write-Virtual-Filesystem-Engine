#include "cowfs/storage/superblock.hpp"
#include "cowfs/common/crc32.hpp"
#include "cowfs/common/align.hpp"
#include <cstring>

namespace cowfs {

SuperblockManager::SuperblockManager() : active_slot_(SUPERBLOCK_A_BLOCK) {
    std::memset(&active_header_, 0, sizeof(active_header_));
}

uint32_t SuperblockManager::compute_checksum(const SuperblockHeader& sb) {
    // Checksum all bytes up to the checksum field itself (offset 92)
    size_t bytes_to_hash = offsetof(SuperblockHeader, checksum);
    return CRC32::calculate(&sb, bytes_to_hash);
}

bool SuperblockManager::verify_checksum(const SuperblockHeader& sb) {
    if (sb.magic != COWFS_MAGIC || sb.version != COWFS_VERSION) {
        return false;
    }
    return compute_checksum(sb) == sb.checksum;
}

Result<SuperblockHeader> SuperblockManager::create_default(const DiskGeometry& geo) {
    SuperblockHeader sb;
    std::memset(&sb, 0, sizeof(sb));

    sb.magic = COWFS_MAGIC;
    sb.version = COWFS_VERSION;
    sb.block_size = BLOCK_SIZE;
    sb.total_blocks = geo.total_blocks;
    sb.free_blocks_count = geo.data_blocks;
    sb.total_inodes = geo.total_inodes;
    sb.free_inodes_count = geo.total_inodes - 1; // Inode 1 reserved for root
    sb.root_inode_id = ROOT_INODE_ID;
    sb.active_generation = 1;
    sb.last_mount_time = current_time_ns();
    sb.last_write_time = current_time_ns();

    sb.bitmap_start_block = geo.bitmap_start;
    sb.bitmap_blocks_count = geo.bitmap_blocks;
    sb.refcount_start_block = geo.refcount_start;
    sb.refcount_blocks_count = geo.refcount_blocks;
    sb.inode_table_start_block = geo.inode_table_start;
    sb.inode_table_blocks_count = geo.inode_table_blocks;
    sb.data_start_block = geo.data_start;
    sb.snapshot_count = 0;

    sb.checksum = compute_checksum(sb);
    return Result<SuperblockHeader>::ok(sb);
}

Result<void> SuperblockManager::load(IBlockDevice& device) {
    AlignedBuffer buf_a(BLOCK_SIZE);
    AlignedBuffer buf_b(BLOCK_SIZE);

    bool a_valid = false;
    bool b_valid = false;

    SuperblockHeader sb_a;
    SuperblockHeader sb_b;

    if (device.read_block(SUPERBLOCK_A_BLOCK, buf_a.data()).is_ok()) {
        std::memcpy(&sb_a, buf_a.data(), sizeof(SuperblockHeader));
        a_valid = verify_checksum(sb_a);
    }

    if (device.read_block(SUPERBLOCK_B_BLOCK, buf_b.data()).is_ok()) {
        std::memcpy(&sb_b, buf_b.data(), sizeof(SuperblockHeader));
        b_valid = verify_checksum(sb_b);
    }

    if (!a_valid && !b_valid) {
        return Result<void>::err(FsError::CorruptedData);
    }

    if (a_valid && b_valid) {
        // Pick the superblock with the higher generation counter
        if (sb_a.active_generation >= sb_b.active_generation) {
            active_header_ = sb_a;
            active_slot_ = SUPERBLOCK_A_BLOCK;
        } else {
            active_header_ = sb_b;
            active_slot_ = SUPERBLOCK_B_BLOCK;
        }
    } else if (a_valid) {
        active_header_ = sb_a;
        active_slot_ = SUPERBLOCK_A_BLOCK;
    } else {
        active_header_ = sb_b;
        active_slot_ = SUPERBLOCK_B_BLOCK;
    }

    return Result<void>::ok();
}

Result<void> SuperblockManager::commit(IBlockDevice& device, const SuperblockHeader& updated) {
    // Toggle slot: if current is A, commit to B; if current is B, commit to A
    block_id_t next_slot = (active_slot_ == SUPERBLOCK_A_BLOCK) ? SUPERBLOCK_B_BLOCK : SUPERBLOCK_A_BLOCK;

    SuperblockHeader next_sb = updated;
    next_sb.active_generation = active_header_.active_generation + 1;
    next_sb.last_write_time = current_time_ns();
    next_sb.checksum = compute_checksum(next_sb);

    AlignedBuffer buf(BLOCK_SIZE);
    std::memcpy(buf.data(), &next_sb, sizeof(SuperblockHeader));

    auto write_res = device.write_block(next_slot, buf.data());
    if (write_res.is_err()) return write_res;

    // Flush to storage medium
    auto sync_res = device.sync();
    if (sync_res.is_err()) return sync_res;

    // Only switch active after successful sync (Atomic shadow commit)
    active_header_ = next_sb;
    active_slot_ = next_slot;

    return Result<void>::ok();
}

} // namespace cowfs
