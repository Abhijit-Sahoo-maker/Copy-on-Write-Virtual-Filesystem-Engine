#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <chrono>

namespace cowfs {

// Fundamental storage unit sizes (Architecture & Hardware)
constexpr size_t BLOCK_SIZE = 4096;        // 4 KB page / filesystem block
constexpr size_t SECTOR_SIZE = 512;        // 512-byte physical disk sector
constexpr size_t CACHE_LINE_SIZE = 64;     // 64-byte L1/L2/L3 cache line width

// Typedefs for clarity and type safety
using block_id_t = uint32_t;
using inode_id_t = uint32_t;
using generation_t = uint64_t;
using checksum_t = uint32_t;
using timestamp_t = uint64_t;

// Special block and inode identifiers
constexpr block_id_t INVALID_BLOCK = 0xFFFFFFFF;
constexpr inode_id_t INVALID_INODE = 0xFFFFFFFF;
constexpr inode_id_t ROOT_INODE_ID = 1;

// Dual Superblock disk locations for atomic crash-consistent commits
constexpr block_id_t SUPERBLOCK_A_BLOCK = 0;
constexpr block_id_t SUPERBLOCK_B_BLOCK = 1;
constexpr block_id_t METADATA_START_BLOCK = 2;

// File system magic number: "COWFS01\0"
constexpr uint64_t COWFS_MAGIC = 0x30315346574F43ULL; 
constexpr uint32_t COWFS_VERSION = 1;

// Inode types (Linux POSIX VFS emulation)
enum class FileType : uint8_t {
    Unknown = 0,
    Regular = 1,
    Directory = 2,
    Symlink = 3
};

// Open flags (POSIX emulation)
namespace OpenFlags {
    constexpr int ReadOnly  = 0x0001;
    constexpr int WriteOnly = 0x0002;
    constexpr int ReadWrite = 0x0004;
    constexpr int Create    = 0x0008;
    constexpr int Truncate  = 0x0010;
    constexpr int Append    = 0x0020;
}

// Seek modes (POSIX lseek)
enum class SeekOrigin {
    Set = 0,      // SEEK_SET
    Current = 1,  // SEEK_CUR
    End = 2       // SEEK_END
};

// Helper for current epoch timestamp in nanoseconds
inline timestamp_t current_time_ns() {
    return static_cast<timestamp_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

} // namespace cowfs
