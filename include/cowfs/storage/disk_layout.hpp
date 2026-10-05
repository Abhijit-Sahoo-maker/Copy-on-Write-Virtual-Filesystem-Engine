#pragma once

#include "cowfs/common/types.hpp"
#include <cstdint>

namespace cowfs {

// On-disk structural constants
constexpr uint32_t INODES_PER_BLOCK = BLOCK_SIZE / 128; // 32 inodes per 4KB block (128 bytes each)
constexpr uint32_t REFCOUNTS_PER_BLOCK = BLOCK_SIZE / sizeof(uint16_t); // 2048 refcounts per 4KB block
constexpr uint32_t BITS_PER_BLOCK = BLOCK_SIZE * 8; // 32,768 bits per 4KB bitmap block

struct DiskGeometry {
    uint64_t total_blocks{0};
    uint64_t total_inodes{0};
    
    // Block offsets
    block_id_t superblock_a{SUPERBLOCK_A_BLOCK};
    block_id_t superblock_b{SUPERBLOCK_B_BLOCK};
    
    block_id_t bitmap_start{METADATA_START_BLOCK};
    uint32_t bitmap_blocks{0};
    
    block_id_t refcount_start{0};
    uint32_t refcount_blocks{0};
    
    block_id_t inode_table_start{0};
    uint32_t inode_table_blocks{0};
    
    block_id_t data_start{0};
    uint32_t data_blocks{0};

    static DiskGeometry compute(uint64_t total_blocks) {
        DiskGeometry geo;
        geo.total_blocks = total_blocks;
        
        // Let inodes scale with disk size (approx 1 inode per 4 data blocks, or min 64)
        uint64_t raw_inodes = (total_blocks > 256) ? (total_blocks / 4) : 64;
        geo.total_inodes = ((raw_inodes + INODES_PER_BLOCK - 1) / INODES_PER_BLOCK) * INODES_PER_BLOCK;

        // Bitmap: 1 bit per total block
        geo.bitmap_start = METADATA_START_BLOCK;
        geo.bitmap_blocks = static_cast<uint32_t>((total_blocks + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK);

        // Refcount: 2 bytes per block
        geo.refcount_start = geo.bitmap_start + geo.bitmap_blocks;
        geo.refcount_blocks = static_cast<uint32_t>((total_blocks + REFCOUNTS_PER_BLOCK - 1) / REFCOUNTS_PER_BLOCK);

        // Inode table:
        geo.inode_table_start = geo.refcount_start + geo.refcount_blocks;
        geo.inode_table_blocks = static_cast<uint32_t>((geo.total_inodes + INODES_PER_BLOCK - 1) / INODES_PER_BLOCK);

        // Data area:
        geo.data_start = geo.inode_table_start + geo.inode_table_blocks;
        geo.data_blocks = (total_blocks > geo.data_start) ? static_cast<uint32_t>(total_blocks - geo.data_start) : 0;

        return geo;
    }
};

} // namespace cowfs
