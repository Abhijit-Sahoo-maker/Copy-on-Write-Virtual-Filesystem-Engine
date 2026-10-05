#pragma once

#include "cowfs/common/types.hpp"
#include "cowfs/common/crc32.hpp"
#include <cstdint>
#include <cstring>
#include <vector>

namespace cowfs {

constexpr size_t INODE_DIRECT_POINTERS = 10;
constexpr size_t POINTERS_PER_BLOCK = BLOCK_SIZE / sizeof(block_id_t); // 1024

#pragma pack(push, 1)
struct DiskInode {
    inode_id_t inode_id;                // 4 bytes
    uint8_t file_type;                  // 1 byte (FileType enum)
    uint8_t reserved1;                  // 1 byte
    uint16_t mode;                      // 2 bytes (permissions)
    uint16_t uid;                       // 2 bytes
    uint16_t gid;                       // 2 bytes
    uint32_t blocks_count;              // 4 bytes
    uint64_t size;                      // 8 bytes (file size in bytes)
    uint64_t atime;                     // 8 bytes (access time)
    uint64_t mtime;                     // 8 bytes (modification time)
    uint64_t ctime;                     // 8 bytes (change time)
    uint64_t generation;                // 8 bytes (CoW mutation counter)

    // Block pointers
    block_id_t direct[INODE_DIRECT_POINTERS]; // 40 bytes (10 direct pointers = 40 KB)
    block_id_t indirect;                // 4 bytes (Single indirect = 1024 * 4KB = 4 MB)
    block_id_t double_indirect;         // 4 bytes (Double indirect = 1024 * 1024 * 4KB = 4 GB)

    uint32_t checksum;                  // 4 bytes (CRC32)
    uint8_t padding[128 - 108];         // 20 bytes padding to 128 bytes total

    void reset() {
        std::memset(this, 0, sizeof(DiskInode));
        inode_id = INVALID_INODE;
        indirect = INVALID_BLOCK;
        double_indirect = INVALID_BLOCK;
        for (size_t i = 0; i < INODE_DIRECT_POINTERS; ++i) {
            direct[i] = INVALID_BLOCK;
        }
    }

    uint32_t compute_checksum() const {
        return CRC32::calculate(this, offsetof(DiskInode, checksum));
    }

    bool verify_checksum() const {
        return compute_checksum() == checksum;
    }

    void update_checksum() {
        checksum = compute_checksum();
    }
};
#pragma pack(pop)

static_assert(sizeof(DiskInode) == 128, "DiskInode must be exactly 128 bytes");

} // namespace cowfs
