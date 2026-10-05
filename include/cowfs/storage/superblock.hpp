#pragma once

#include "cowfs/common/types.hpp"
#include "cowfs/common/result.hpp"
#include "cowfs/storage/disk_layout.hpp"
#include "cowfs/hardware/block_device.hpp"
#include <cstring>

namespace cowfs {

#pragma pack(push, 1)
struct SuperblockHeader {
    uint64_t magic;                     // COWFS_MAGIC (8)
    uint32_t version;                   // COWFS_VERSION (4)
    uint32_t block_size;                // 4096 (4)
    uint64_t total_blocks;              // (8)
    uint64_t free_blocks_count;         // (8)
    uint64_t total_inodes;              // (8)
    uint64_t free_inodes_count;         // (8)
    uint32_t root_inode_id;             // (4)
    uint64_t active_generation;         // Monotonically increasing generation (8)
    uint64_t last_mount_time;           // (8)
    uint64_t last_write_time;           // (8)
    
    // Geometry layout
    uint32_t bitmap_start_block;        // (4)
    uint32_t bitmap_blocks_count;       // (4)
    uint32_t refcount_start_block;      // (4)
    uint32_t refcount_blocks_count;     // (4)
    uint32_t inode_table_start_block;   // (4)
    uint32_t inode_table_blocks_count;  // (4)
    uint32_t data_start_block;          // (4)
    uint32_t snapshot_count;            // (4)
    
    uint32_t checksum;                  // CRC32 of all previous fields (4)
    uint8_t  padding[BLOCK_SIZE - 112]; // Exact 4096 bytes (112 + 3984 = 4096)
};
#pragma pack(pop)

static_assert(sizeof(SuperblockHeader) == BLOCK_SIZE, "Superblock must be exactly 4096 bytes (1 block)");

class SuperblockManager {
public:
    SuperblockManager();

    // Format new filesystem superblocks
    static Result<SuperblockHeader> create_default(const DiskGeometry& geo);

    // Read and validate dual superblocks; chooses the one with higher generation and valid CRC
    Result<void> load(IBlockDevice& device);

    // Atomically commit an updated superblock by writing to the alternate slot with generation+1
    Result<void> commit(IBlockDevice& device, const SuperblockHeader& updated);

    const SuperblockHeader& get() const noexcept { return active_header_; }
    SuperblockHeader& get_mut() noexcept { return active_header_; }
    block_id_t get_active_slot() const noexcept { return active_slot_; }

    static uint32_t compute_checksum(const SuperblockHeader& sb);
    static bool verify_checksum(const SuperblockHeader& sb);

private:
    SuperblockHeader active_header_;
    block_id_t active_slot_; // SUPERBLOCK_A_BLOCK or SUPERBLOCK_B_BLOCK
};

} // namespace cowfs
