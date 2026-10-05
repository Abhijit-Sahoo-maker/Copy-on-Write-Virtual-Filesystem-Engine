#pragma once

#include "cowfs/common/types.hpp"
#include "cowfs/common/result.hpp"
#include "cowfs/hardware/block_device.hpp"
#include "cowfs/storage/disk_layout.hpp"
#include "cowfs/storage/superblock.hpp"
#include "cowfs/storage/bitmap_allocator.hpp"
#include "cowfs/storage/refcount_table.hpp"
#include "cowfs/cache/buffer_pool.hpp"
#include "cowfs/core/inode.hpp"
#include "cowfs/core/directory.hpp"
#include "cowfs/core/snapshot.hpp"
#include <memory>
#include <string>
#include <vector>
#include <shared_mutex>
#include <unordered_map>

namespace cowfs {

struct FileStat {
    inode_id_t inode_id;
    FileType type;
    uint64_t size;
    uint32_t blocks_count;
    uint16_t mode;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint64_t generation;
    std::vector<block_id_t> allocated_blocks;
};

struct FsStatus {
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t used_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
    uint64_t used_inodes;
    uint64_t active_generation;
    uint32_t snapshot_count;
    double block_utilization_pct;
    double cache_hit_ratio;
    double write_amplification;
};

class FileSystem {
public:
    explicit FileSystem(std::unique_ptr<IBlockDevice> device, size_t cache_frames = 256);
    ~FileSystem();

    // Format a disk device with a fresh CowFS layout
    static Result<void> format(IBlockDevice& device, uint64_t total_blocks);

    // Mount an existing formatted filesystem
    Result<void> mount();

    // Unmount safely, flushing all dirty pages and metadata
    Result<void> unmount();

    bool is_mounted() const noexcept { return is_mounted_; }

    // File operations
    Result<inode_id_t> create_file(const std::string& path, FileType type, uint16_t mode = 0644);
    Result<size_t> read_file(inode_id_t inode_id, uint64_t offset, uint8_t* buffer, size_t count);
    Result<size_t> write_file(inode_id_t inode_id, uint64_t offset, const uint8_t* buffer, size_t count);
    Result<void> truncate_file(inode_id_t inode_id, uint64_t new_size);
    Result<void> remove_file(const std::string& path);
    Result<FileStat> stat_file(inode_id_t inode_id);
    Result<inode_id_t> lookup_path(const std::string& path);

    // CoW Instant File Cloning (cp src dst with zero block duplication)
    Result<inode_id_t> clone_file(const std::string& src_path, const std::string& dst_path);

    // Directory operations
    Result<void> create_directory(const std::string& path, uint16_t mode = 0755);
    Result<std::vector<DirEntry>> list_directory(const std::string& path);
    Result<void> remove_directory(const std::string& path);

    // Snapshot operations (O(1) Copy-on-Write snapshots)
    Result<void> create_snapshot(const std::string& name);
    Result<void> restore_snapshot(const std::string& name);
    Result<std::vector<SnapshotInfo>> list_snapshots();
    Result<void> delete_snapshot(const std::string& name);

    // Deduplication pass: merges identical data blocks across files
    Result<size_t> deduplicate_blocks();

    // Sync all dirty blocks and commit superblock
    Result<void> sync();

    // Status & Diagnostics
    FsStatus get_status();
    const CacheStats& get_cache_stats() const { return buffer_pool_->get_stats(); }
    const BlockDeviceStats& get_io_stats() const { return device_->get_stats(); }
    IBlockDevice& get_device() { return *device_; }

    // Internal low-level block and inode inspection (for shell/debug)
    Result<DiskInode> get_raw_inode(inode_id_t inode_id);
    Result<uint16_t> get_block_refcount(block_id_t block_id);

private:
    std::unique_ptr<IBlockDevice> device_;
    std::unique_ptr<BufferPool> buffer_pool_;
    SuperblockManager superblock_mgr_;
    std::unique_ptr<BitmapAllocator> allocator_;
    std::unique_ptr<RefCountTable> refcount_table_;
    DiskGeometry geometry_;
    bool is_mounted_{false};
    mutable std::shared_mutex fs_mutex_;

    // Snapshot in-memory registry
    std::vector<SnapshotInfo> snapshots_;

    // Inode table helpers
    Result<inode_id_t> allocate_inode();
    Result<void> free_inode(inode_id_t inode_id);
    Result<DiskInode> read_inode(inode_id_t inode_id);
    Result<void> write_inode(const DiskInode& inode);

    // Block mapping & Copy-on-Write write helper
    Result<block_id_t> get_or_allocate_file_block(DiskInode& inode, uint32_t logical_block_idx, bool for_write);
    Result<void> free_all_file_blocks(DiskInode& inode);

    // Path resolution helper
    Result<std::pair<inode_id_t, std::string>> resolve_parent_and_basename(const std::string& path);
    Result<inode_id_t> resolve_path_internal(const std::string& path);

    // Directory block helpers
    Result<void> dir_add_entry(DiskInode& dir_inode, const DirEntry& entry);
    Result<void> dir_remove_entry(DiskInode& dir_inode, std::string_view name);
    Result<DirEntry> dir_find_entry(DiskInode& dir_inode, std::string_view name);
    Result<std::vector<DirEntry>> dir_get_all_entries(DiskInode& dir_inode);

    // Deep copy helper for snapshotting
    Result<inode_id_t> clone_inode_tree(inode_id_t root_id);
    Result<void> free_inode_tree(inode_id_t inode_id);
};

} // namespace cowfs
