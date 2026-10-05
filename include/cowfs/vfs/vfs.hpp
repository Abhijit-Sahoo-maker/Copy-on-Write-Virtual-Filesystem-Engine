#pragma once

#include "cowfs/core/filesystem.hpp"
#include "cowfs/vfs/file_descriptor.hpp"
#include <memory>
#include <string>
#include <vector>

namespace cowfs {

class VFS {
public:
    VFS();
    ~VFS();

    // Volume Management
    int mkfs(const std::string& dev_path, uint64_t total_blocks);
    int mount(const std::string& dev_path);
    int unmount();
    bool is_mounted() const;

    // File operations (POSIX emulation)
    int open(const std::string& path, int flags, int mode = 0644);
    int close(int fd);
    ssize_t read(int fd, void* buf, size_t count);
    ssize_t write(int fd, const void* buf, size_t count);
    int64_t lseek(int fd, int64_t offset, SeekOrigin whence);
    int unlink(const std::string& path);
    int stat(const std::string& path, FileStat* st);

    // Directory operations
    int mkdir(const std::string& path, int mode = 0755);
    int rmdir(const std::string& path);
    std::vector<DirEntry> ls(const std::string& path);

    // Copy-on-Write Features
    int clone(const std::string& src_path, const std::string& dst_path);
    int snapshot_create(const std::string& name);
    int snapshot_restore(const std::string& name);
    int snapshot_delete(const std::string& name);
    std::vector<SnapshotInfo> snapshot_list();
    size_t dedup();

    // Synchronization & Status
    int sync();
    FsStatus status();
    const CacheStats* cache_stats() const;
    const BlockDeviceStats* io_stats() const;
    FileSystem* get_filesystem() { return fs_.get(); }

private:
    std::unique_ptr<FileSystem> fs_;
    FileTable file_table_;
};

} // namespace cowfs
